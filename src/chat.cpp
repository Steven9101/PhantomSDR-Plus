// chat.cpp
#include "spectrumserver.h"
#include "chat.h"
#include "logger.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <regex>
#include <random>
#include <ctime>

std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> ChatClient::chat_connections;
std::mutex ChatClient::chat_connections_mutex;
std::deque<ChatMessage> ChatClient::chat_messages_history;
std::mutex ChatClient::chat_history_mutex;
bool ChatClient::is_history_loaded = false;
uint64_t ChatClient::message_counter = 0;

const std::set<std::string> ChatClient::blocked_usernames = {
    "admin", "operator", "host", "root", "system", "moderator"
};

const std::set<std::string> ChatClient::blocked_words = {
    "fuck", "fucking", "bitch", "shit", "ass", "bitch", "cunt", "bastard", "idiot", "moron", "dumb", "stupid", "loser", "dummy", "moron", "retard", "dumbass", "asshole", "idiot"
};

ChatClient::ChatClient(connection_hdl hdl, PacketSender &sender)
    : Client(hdl, sender, CHAT) {
    {
        std::lock_guard<std::mutex> lock(chat_history_mutex);
        if (!is_history_loaded) {
            load_chat_history();
            is_history_loaded = true;
        }
    }
    on_open_chat(hdl);
}
std::string ChatClient::get_or_generate_username(const std::string& user_id) {
    auto it = user_id_to_name.find(user_id);
    if (it != user_id_to_name.end()) {
        return it->second;
    } else {
        std::hash<std::string> hasher;
        auto hashed = hasher(user_id);
        std::string numeric_part = std::to_string(hashed).substr(0, 6);
        std::string username = "user" + numeric_part;
        
        if (is_valid_username(username)) {
            user_id_to_name[user_id] = username;
            return username;
        } else {
            return get_or_generate_username(user_id + "1");
        }
    }
}

bool ChatClient::is_valid_username(const std::string& username) {
    return blocked_usernames.find(username) == blocked_usernames.end();
}

std::string ChatClient::filter_message(const std::string& message) {
    std::string filtered_message = message;

    // Filter out swear words
    for (const auto& word : blocked_words) {
        std::regex word_regex("\\b" + word + "\\b", std::regex_constants::icase);
        filtered_message = std::regex_replace(filtered_message, word_regex, std::string(word.length(), '*'));
    }

    return filtered_message;
}

std::string ChatClient::generate_message_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(timestamp) + "_" + std::to_string(++message_counter);
}

std::string ChatClient::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    struct tm* timeinfo = std::localtime(&now_c);
    if (timeinfo != nullptr) {
        ss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
        ss << "[timestamp error]";
    }
    return ss.str();
}

std::string ChatClient::escape_json_string(const std::string& input) {
    std::string escaped;
    escaped.reserve(input.length() * 2); // Reserve space to avoid frequent reallocations
    
    for (char c : input) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b";  break;
            case '\f': escaped += "\\f";  break;
            case '\n': escaped += "\\n";  break;
            case '\r': escaped += "\\r";  break;
            case '\t': escaped += "\\t";  break;
            default:
                if (c >= 0 && c < 32) {
                    // Control characters
                    escaped += "\\u";
                    escaped += "0000";
                    escaped[escaped.length() - 2] = "0123456789abcdef"[(c >> 4) & 0xF];
                    escaped[escaped.length() - 1] = "0123456789abcdef"[c & 0xF];
                } else {
                    escaped += c;
                }
                break;
        }
    }
    return escaped;
}

std::string ChatClient::chat_message_to_json(const ChatMessage& msg) {
    std::stringstream json;
    json << "{"
         << "\"id\":\"" << escape_json_string(msg.id) << "\","
         << "\"username\":\"" << escape_json_string(msg.username) << "\","
         << "\"message\":\"" << escape_json_string(msg.message) << "\","
         << "\"timestamp\":\"" << escape_json_string(msg.timestamp) << "\","
         << "\"user_id\":\"" << escape_json_string(msg.user_id) << "\","
         << "\"type\":\"" << escape_json_string(msg.type) << "\","
         << "\"reply_to_id\":\"" << escape_json_string(msg.reply_to_id) << "\","
         << "\"reply_to_username\":\"" << escape_json_string(msg.reply_to_username) << "\""
         << "}";
    return json.str();
}

ChatMessage ChatClient::json_to_chat_message(const std::string& json) {
    ChatMessage msg;
    try {
        // Basic JSON parsing - extract values between quotes
        std::regex id_regex("\"id\":\\s*\"([^\"]+)\"");
        std::regex username_regex("\"username\":\\s*\"([^\"]+)\"");
        std::regex message_regex("\"message\":\\s*\"([^\"]+)\"");
        std::regex timestamp_regex("\"timestamp\":\\s*\"([^\"]+)\"");
        std::regex user_id_regex("\"user_id\":\\s*\"([^\"]+)\"");
        std::regex type_regex("\"type\":\\s*\"([^\"]+)\"");
        std::regex reply_to_id_regex("\"reply_to_id\":\\s*\"([^\"]+)\"");
        std::regex reply_to_username_regex("\"reply_to_username\":\\s*\"([^\"]+)\"");
        
        std::smatch match;
        if (std::regex_search(json, match, id_regex)) msg.id = match[1];
        if (std::regex_search(json, match, username_regex)) msg.username = match[1];
        if (std::regex_search(json, match, message_regex)) msg.message = match[1];
        if (std::regex_search(json, match, timestamp_regex)) msg.timestamp = match[1];
        if (std::regex_search(json, match, user_id_regex)) msg.user_id = match[1];
        if (std::regex_search(json, match, type_regex)) msg.type = match[1];
        if (std::regex_search(json, match, reply_to_id_regex)) msg.reply_to_id = match[1];
        if (std::regex_search(json, match, reply_to_username_regex)) msg.reply_to_username = match[1];
    } catch (...) {
        // Return empty message on parse error
        msg = ChatMessage{};
    }
    return msg;
}

bool ChatClient::is_valid_json_message(const std::string& json) {
    try {
        ChatMessage msg = json_to_chat_message(json);
        return !msg.id.empty() && !msg.username.empty() && !msg.message.empty();
    } catch (...) {
        return false;
    }
}


void ChatClient::store_chat_message(const ChatMessage& message) {
    std::lock_guard<std::mutex> lock(chat_history_mutex);
    if(chat_messages_history.size() >= 20) 
    {
        chat_messages_history.pop_front();
    }
    chat_messages_history.push_back(message);
    
    // Save without calling get_chat_history_as_json to avoid deadlock
    try {
        std::ofstream json_file("chat_history.json", std::ios::trunc);
        if (json_file.is_open()) {
            json_file << "[";
            for (size_t i = 0; i < chat_messages_history.size(); ++i) {
                json_file << chat_message_to_json(chat_messages_history[i]);
                if (i < chat_messages_history.size() - 1) {
                    json_file << ",";
                }
            }
            json_file << "]";
            json_file.close();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Chat", "Error saving chat history: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Chat", "Unknown error saving chat history");
    }
}

std::string ChatClient::get_chat_history_as_json() {
    std::lock_guard<std::mutex> lock(chat_history_mutex);
    std::string history = "[";
    for (size_t i = 0; i < chat_messages_history.size(); ++i) {
        history += chat_message_to_json(chat_messages_history[i]);
        if (i < chat_messages_history.size() - 1) {
            history += ",";
        }
    }
    history += "]";
    return history;
}


void ChatClient::on_chat_message(connection_hdl sender_hdl, std::string& user_id, std::string& username, std::string& message, std::string& reply_to_id, std::string& reply_to_username) {
    try {
        // Input validation
        const size_t MAX_USERNAME_LENGTH = 14;
        const size_t MAX_MESSAGE_LENGTH = 200;

        // Prevent empty or overly long user_id that could cause issues
        if (user_id.empty() || user_id.length() > 100) {
            LOG_WARNING("Chat", "Invalid user_id received, using fallback");
            user_id = "invalid_" + std::to_string(rand() % 1000000);
        }

        // Use the provided user_id from the frontend

        // Trim leading and trailing spaces from the username
        size_t first = username.find_first_not_of(" \t\n\r\f\v");
        if (first != std::string::npos) {
            username.erase(0, first);
            size_t last = username.find_last_not_of(" \t\n\r\f\v");
            if (last != std::string::npos) {
                username.erase(last + 1);
            }
        } else {
            username = "user" + std::to_string(rand() % 1000000);
        }

        if (username.length() > MAX_USERNAME_LENGTH) {
            username = username.substr(0, MAX_USERNAME_LENGTH);
        }

        if (message.length() > MAX_MESSAGE_LENGTH) {
            message = message.substr(0, MAX_MESSAGE_LENGTH);
        }

        // Check if the username is blocked
        if (!is_valid_username(username)) {
            // Generate a random username
            std::string random_username = "user" + std::to_string(rand() % 1000000);
            LOG_WARNING("Chat", "Blocked username '" + username + "' detected. Assigned random username: " + random_username);
            username = random_username;
        }

        std::string filtered_message = filter_message(message);
        
        // Create structured message
        ChatMessage chat_msg;
        chat_msg.id = generate_message_id();
        chat_msg.username = username;
        chat_msg.message = filtered_message;
        chat_msg.timestamp = get_timestamp();
        chat_msg.user_id = user_id;
        chat_msg.type = "message";
        chat_msg.reply_to_id = reply_to_id;
        chat_msg.reply_to_username = reply_to_username;

        // Store message
        store_chat_message(chat_msg);

        // Convert to JSON for transmission
        std::string json_message = chat_message_to_json(chat_msg);

        // Copy connections while holding the lock, then send outside the lock
        std::vector<websocketpp::connection_hdl> connections_copy;
        {
            std::lock_guard<std::mutex> lock(chat_connections_mutex);
            connections_copy.reserve(chat_connections.size());
            for (const auto& conn : chat_connections) {
                connections_copy.push_back(conn);
            }
        }
        
        // Send to all connections outside of the lock
        for (const auto& conn : connections_copy) {
            try {
                sender.send_text_packet(conn, json_message);
            } catch (const std::exception& e) {
                LOG_ERROR("Chat", "Failed to send message to connection: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Chat", "Unknown error sending message to connection");
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Chat", "Error in on_chat_message: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Chat", "Unknown error in on_chat_message");
    }
}

void ChatClient::on_open_chat(connection_hdl hdl) {
    try {
        {
            std::lock_guard<std::mutex> lock(chat_connections_mutex);
            chat_connections.insert(hdl);
        }
        
        // Get history as JSON without holding the lock during network operation
        std::string history_json = get_chat_history_as_json();
        
        // Send history outside of any locks
        if (history_json != "[]") {
            std::string history_message = "{\"type\":\"history\",\"messages\":" + history_json + "}";
            try {
                sender.send_text_packet(hdl, history_message);
            } catch (...) {
                // Connection might be closed, ignore
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Chat", "Error in on_open_chat: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Chat", "Unknown error in on_open_chat");
    }
}


void ChatClient::on_close_chat(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(chat_connections_mutex);
    chat_connections.erase(hdl);
}

void ChatClient::load_chat_history() {
    try {
        // Try to load from new JSON format first
        std::ifstream json_file("chat_history.json");
        if (json_file.is_open()) {
            std::string json_content((std::istreambuf_iterator<char>(json_file)),
                                     std::istreambuf_iterator<char>());
            json_file.close();
            
            if (!json_content.empty() && json_content != "[]") {
                // Parse JSON array
                std::regex msg_regex("\\{[^}]+\\}");
                std::sregex_iterator iter(json_content.begin(), json_content.end(), msg_regex);
                std::sregex_iterator end;
                
                for (; iter != end && chat_messages_history.size() < 20; ++iter) {
                    std::string json_msg = iter->str();
                    if (is_valid_json_message(json_msg)) {
                        ChatMessage msg = json_to_chat_message(json_msg);
                        chat_messages_history.push_back(msg);
                    }
                }
                return;
            }
        }
        
        // One-time migration from old text format
        std::ifstream file("chat_history.txt");
        if (file.is_open()) {
            LOG_INFO("Chat", "Migrating old chat history to new format");
            std::string line;
            while (std::getline(file, line) && chat_messages_history.size() < 20) {
                if (!line.empty()) {
                    // Convert old format to new format
                    std::regex old_format("^(\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}) (.+?): (.+)$");
                    std::smatch match;
                    if (std::regex_match(line, match, old_format)) {
                        ChatMessage msg;
                        msg.id = generate_message_id();
                        msg.timestamp = match[1];
                        msg.username = match[2];
                        msg.message = match[3];
                        msg.user_id = "legacy";
                        msg.type = "message";
                        msg.reply_to_id = "";
                        msg.reply_to_username = "";
                        chat_messages_history.push_back(msg);
                    }
                }
            }
            file.close();
            
            // Save migrated data and remove old file
            if (!chat_messages_history.empty()) {
                save_chat_history();
                std::remove("chat_history.txt");
                LOG_INFO("Chat", "Migration completed successfully");
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Chat", "Error loading chat history: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Chat", "Unknown error loading chat history");
    }
}

void ChatClient::save_chat_history() {
    try {
        // Save only in JSON format (no more backward compatibility)
        // NOTE: This function should only be called when chat_history_mutex is already held
        // to avoid deadlock with get_chat_history_as_json()
        std::ofstream json_file("chat_history.json", std::ios::trunc);
        if (json_file.is_open()) {
            json_file << "[";
            for (size_t i = 0; i < chat_messages_history.size(); ++i) {
                json_file << chat_message_to_json(chat_messages_history[i]);
                if (i < chat_messages_history.size() - 1) {
                    json_file << ",";
                }
            }
            json_file << "]";
            json_file.close();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Chat", "Error saving chat history: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Chat", "Unknown error saving chat history");
    }
}

std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>>& ChatClient::get_chat_connections() {
    return chat_connections;
}

ChatClient::~ChatClient() {
    std::lock_guard<std::mutex> lock(chat_connections_mutex);
    chat_connections.erase(hdl);
}
