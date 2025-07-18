// chat.h
#ifndef CHAT_H
#define CHAT_H

#include "client.h"
#include <string>
#include <unordered_map>
#include <deque>
#include <set>
#include <mutex>
#include <cstdint>

struct ChatMessage {
    std::string id;
    std::string username;
    std::string message;
    std::string timestamp;
    std::string user_id;
    std::string type;
    std::string reply_to_id;
    std::string reply_to_username;
};

class ChatClient : public Client {
public:
    ChatClient(connection_hdl hdl, PacketSender& sender);
    void send_event(std::string& event);
    virtual void on_chat_message(connection_hdl sender_hdl, std::string& user_id, std::string& username, std::string& message, std::string& reply_to_id, std::string& reply_to_username);
    virtual void store_chat_message(const ChatMessage& message);
    virtual void on_close_chat(connection_hdl hdl);
    virtual void on_open_chat(connection_hdl hdl);
    std::string get_or_generate_username(const std::string& user_id);
    std::string get_chat_history_as_json();
    static std::set<connection_hdl, std::owner_less<connection_hdl>>& get_chat_connections();
    virtual ~ChatClient();

private:
    static std::set<connection_hdl, std::owner_less<connection_hdl>> chat_connections;
    static std::mutex chat_connections_mutex;
    std::unordered_map<std::string, std::string> user_id_to_name;
    static std::deque<ChatMessage> chat_messages_history;
    static std::mutex chat_history_mutex;
    static const std::set<std::string> blocked_usernames;
    static const std::set<std::string> blocked_words;
    static bool is_history_loaded;
    static uint64_t message_counter;
    
    void load_chat_history();
    void save_chat_history();
    bool is_valid_username(const std::string& username);
    std::string filter_message(const std::string& message);
    std::string generate_message_id();
    std::string get_timestamp();
    std::string chat_message_to_json(const ChatMessage& msg);
    ChatMessage json_to_chat_message(const std::string& json);
    bool is_valid_json_message(const std::string& json);
    std::string escape_json_string(const std::string& input);
};

#endif