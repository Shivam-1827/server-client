#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <iostream>
#include <vector>
#include <map>
#include <atomic>
#include <chrono>
#include <thread>
#include <memory>
#include <deque>
#include <mutex>

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

// Creating structure of message which will be received from the client
struct ClientMessage{
    uint64_t unique_id;     // 8 byte
    uint64_t timestamp_ns;  // 8 byte
    vector<char>payload;    //112 byte
};

map<uint32_t, deque<ClientMessage>> client_messages;
atomic<uint32_t> next_client_id = {0};

mutex client_messages_mutex;

class tcp_connection : public boost::enable_shared_from_this<tcp_connection> {
    private:

        tcp::socket socket_;
        uint32_t client_id_;
        vector<char>read_buffer_;
        string write_buffer_ack_;

        tcp_connection(io_context& io_context) :
            socket_(io_context),
            client_id_(0) {
                read_buffer_.resize(128);
                write_buffer_ack_ = "ACK";
            }

        void start_read(){
            auto self(shared_from_this());   //keeping the object alive for the callback

            boost::asio::async_read(socket_, buffer(read_buffer_, 128), [self](const boost::system::error_code& error , size_t bytes_transferred) {
                //this is the lambda (callback) is executed when the read operation completes
                self->handle_read(error, bytes_transferred);
            });
        }

        void handle_read(const boost::system::error_code& error, size_t bytes_transferred){
            if(!error){
                //if no error then successfully read rthe message
                ClientMessage msg;
                memcpy(&msg.unique_id, read_buffer_.data(), sizeof(uint64_t));
                memcpy(&msg.timestamp_ns, read_buffer_.data() + sizeof(uint64_t), sizeof(uint64_t));
                size_t payload_size = 128 - sizeof(uint64_t) * 2;

                if(payload_size > 0){
                    msg.payload.assign(read_buffer_.begin() + sizeof(uint64_t)*2, read_buffer_.begin() + sizeof(uint64_t)*2 + payload_size);
                } else {
                    msg.payload.clear();
                }

                // storing message (using mutex protection)
                {
                    lock_guard<mutex>lock(client_messages_mutex);
                    client_messages[client_id_].push_back(msg);

                    // we can  also limit the number of message stored per client to prevent the excessive memory use
                }

                // sending acknowledgement asynchronously
                auto self(shared_from_this());   
                boost::asio::async_write(socket_, buffer(write_buffer_ack_), [self](const boost::system::error_code& write_error, size_t bytes_transferred) {
                    self->handle_write(write_error, bytes_transferred);
                });
            } else if(error == error::eof){
                // connection closed by client
                cout<< "Client " << client_id_ << " disconnected." <<endl;

                // removing client data
                {
                    lock_guard<mutex>lock(client_messages_mutex);
                    client_messages.erase(client_id_);
                }
            } else{
                cerr << "Error on client " << client_id_ << ": " << error.message() << endl;

                {
                    lock_guard<mutex>lock(client_messages_mutex);
                    client_messages.erase(client_id_);
                }

            }
        }

        void handle_write(const boost::system::error_code& error, size_t bytes_transferred){
            if(!error) {
                // if no error then successfully sending acknowledgement 
                start_read();
            } else{
                cerr << "Error writing to client "<< client_id_ << ": " << error.message() <<endl;

                // removing client data
                {
                    lock_guard<mutex>lock(client_messages_mutex);
                    client_messages.erase(client_id_);
                }
            }
        }


    public: 
        static boost::shared_ptr<tcp_connection> create(io_context& io_context){
            return boost::shared_ptr<tcp_connection>(new tcp_connection(io_context));
        }

        tcp::socket& socket(){
            return socket_;
        }

        void start(){
            client_id_ = next_client_id++;
            cout << "New client connected with ID: " << client_id_ << endl;

            start_read();
        }
};

//Server class
class tcp_server{
    public:
        io_context& io_context_;
        tcp::acceptor acceptor_;

        void start_accept() {
            boost::shared_ptr<tcp_connection> new_connection = tcp_connection::create(io_context_);

            acceptor_.async_accept(new_connection->socket(), 
            [this, new_connection](const boost::system::error_code& error){
                handle_accept(new_connection, error);
            });
        }

        void handle_accept(boost::shared_ptr<tcp_connection> new_connection, const boost::system::error_code& error){
            if(!error){
                new_connection->start();
            } else{
                cerr << "Accept error: " << error.message() << endl;
            }
            start_accept();
        }

    public: 
        tcp_server(io_context& io_context, short port):
            io_context_(io_context),
            acceptor_(io_context, tcp::endpoint(tcp::v4(), port)){
                start_accept();
            }
};

int main(){
    try{
        // creating io_context for core I/O functionality and we will run it on multiple threads for concurrency
        boost::asio::io_context io_context;

        //Creating server object listening to port 8080
        tcp_server server(io_context, 8080);

        cout << "Server started , listening to port 8080...." << endl;

        unsigned int num_threads = thread::hardware_concurrency();
        if(num_threads == 0) num_threads = 4;   // setting default to 4 if hardware concurrency is not awailable

        vector<thread>threads;
        //to run multiple io_context on multiple threads we need to create a thread pool and creating thread pool of size equals to number of CPU cores

        for(unsigned int i = 0; i<num_threads; i++) {
            threads.emplace_back( [&io_context](){
                io_context.run();
            });
        }

        // keeping the main thread alive while the other workers threads are running

        for(auto& t: threads){
            t.join();
        }
 
    } catch(const exception &e){
        cerr << "Server exception: "<< e.what() << endl;
    }

    return 0;
}