#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring> // For memcpy
#include <atomic>
#include <iomanip> // For std::fixed and std::setprecision
#include <memory>

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace std;


// Creating structure of message which will be received from the client
struct ClientMessage{
    uint64_t unique_id;     // 8 byte
    uint64_t timestamp_ns;  // 8 byte
    vector<char>payload;    //112 byte
};

class client_connection : public boost::enable_shared_from_this<client_connection> {
    private:
        io_context& io_context_;
        tcp::socket socket_;
        string host_;
        string port_;
        uint64_t message_counter_;
        int num_messages_to_send_;
        vector<char> write_buffer_;
        vector<char> read_buffer_;
        std::chrono::time_point<std::chrono::high_resolution_clock> message_send_time_;   // timestamp before sending a messae

        client_connection(io_context& io_context, const string& host, const string& port) :
            io_context_(io_context), socket_(io_context), host_(host), port_(port) {
                message_counter_ = 0;
                num_messages_to_send_ = 0;  // this we are setting by send_message()
                write_buffer_.resize(128);   // reserved space for read write buffer
                read_buffer_.resize(3);   // for "ack"
            }

        void handle_connect(const boost::system::error_code& error, const tcp::endpoint& endpoint){
            if(!error){
                cout << "Connected to server: " << endpoint << endl;
                // after the connection is successfull we need to call send message
            } else {
                cerr << "Connection error: " << error.what() <<endl;
            }
        }

        void prepare_and_send_message() {
            if(message_counter_ >=num_messages_to_send_) {
                cout << "Finished sending " << num_messages_to_send_ << " messages." <<endl;

                // all message sent, close the socket
                boost::system::error_code ec;
                socket_.shutdown(tcp::socket::shutdown_both, ec);
                socket_.close(ec);
                return;
            }

            ClientMessage msg;

            auto now = std::chrono::high_resolution_clock::now();  //current nano second time stamp

            msg.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();

            // timestamp before sending the message for latency calculation
            message_send_time_ = std::chrono::high_resolution_clock::now();

            // creating payload
            size_t payload_size = 128 - sizeof(uint64_t) * 2;
            string s;
            getline(cin, s);
            msg.payload.resize(payload_size);
            std::copy(s.begin(), s.end(), msg.payload.begin());

            // serializing the message into write buffer
            memcpy(write_buffer_.data(), &msg.unique_id, sizeof(uint64_t));
            memcpy(write_buffer_.data() + sizeof(uint64_t), &msg.timestamp_ns, sizeof(uint64_t));
            memcpy(write_buffer_.data() + sizeof(uint64_t)*2, &msg.payload, payload_size);


            auto self(shared_from_this());  // keep the object alive for the callback

            // initiating an asynchronous write operation
            boost::asio::async_write(socket_, buffer(write_buffer_), [self, sent_msg_id = msg.unique_id, sent_timestamp_ns = msg.timestamp_ns](const boost::system::error_code& error, size_t bytes_tranferred) {
                self->handle_write(error, bytes_tranferred, sent_msg_id, sent_timestamp_ns);
            });

            message_counter_++;
        }

        void handle_write(const boost::system::error_code& error, size_t bytes_transferred, uint64_t sent_msg_id, uint64_t sent_timestamp_ns) {
            if(!error){

                //successfully sent message

                auto self(shared_from_this());
                boost::asio::async_read(socket_, buffer(read_buffer_), [self, sent_msg_id, sent_timestamp_ns](const boost::system::error_code& read_error, size_t bytes_transferred){
                    self->handle_read_ack(read_error, bytes_transferred, sent_msg_id, sent_timestamp_ns);
                });
            } else {
                // error during write
                cerr << "Write error for message " << sent_msg_id << ": " <<error.what() << endl;

                // closing socket on error
                boost::system::error_code ec;
                socket_.close(ec);
            }
        }

        void handle_read_ack(const boost::system::error_code& error, size_t bytes_transferred, uint64_t sent_msg_id, uint64_t sent_timestamp_ns) {
            if(!error){
                // successfully received the acknowledgement
                auto receive_time = std::chrono::high_resolution_clock::now();

                // verify acknowledgement
                string ack(read_buffer_.begin(), read_buffer_.begin() + bytes_transferred);

                if(ack == "ACK") {
                    // calculating and displaying latency
                    auto latency_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(receive_time - message_send_time_).count();

                    cout << fixed << setprecision(0) << "Msg ID: "<< sent_msg_id << ", Sent Ts (ns): " <<sent_timestamp_ns << ", Latency (ns): " << latency_ns << endl;

                    // send the next message (if any more to send)
                    prepare_and_send_message(); 
                } else{
                    cerr << "Received unexpected acknowledgement for the message ID " << sent_msg_id << ": " << ack << endl;

                    // close the socket error
                    boost::system::error_code ec;
                    socket_.close(ec);            
                }
            } else if(error == error::eof){
                cout << "Server closed connection for message ID: "<< sent_msg_id << endl;
            } else {
                // error during read of acknowledgement
                cerr << "Read error for acknowledgement message ID: " << sent_msg_id << " -> " << error.what()<<endl;

                // close the socket on error
                boost::system::error_code ec;
                socket_.close(ec);
            }
        }
    
    public:
        
        static boost::shared_ptr<client_connection> create(io_context& io_context, const string& host, const string& port){
        return boost::shared_ptr<client_connection>(new client_connection(io_context, host, port));
        }

        void start(){
            tcp::resolver resolver(io_context_);
            tcp::resolver::results_type endpoints = resolver.resolve(host_, port_);

            // initiate asynchronous connection
            // when connected, the handle_connect function is called
            boost::asio::async_connect(socket_, endpoints, [self = shared_from_this()](const boost::system::error_code& error, const tcp::endpoint& endpoint) {
                self->handle_connect(error, endpoint);
            });
        }

        void send_messages(int num_messages_to_send){
            num_messages_to_send_ = num_messages_to_send;
            send_next_message();
        }

        void send_next_message(){
            prepare_and_send_message();
        }


};


int main(){
    try{
        //creating io_context
        boost::asio::io_context io_context;

        // creating client connection object
        boost::shared_ptr<client_connection>client = client_connection::create(io_context, "127.0.0.1", "8080");

        //starting connection
        client->start();

        int num_messages = 10;  //sending 10 messages for demonstration

        client->send_messages(num_messages);

        io_context.run();  // this will run an event loop, processign all asynchronous operations until there is no work to do

        cout << "Client finished." << endl;
    } catch(const exception& e){
        cerr << "Client exception: " << e.what() <<endl;
    }
}