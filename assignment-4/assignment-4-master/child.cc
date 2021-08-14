#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <grpc++/grpc++.h>
#include "assign4.grpc.pb.h"


using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

using assign4::Database;
using assign4::Request_word;
using assign4::Response_word;
using assign4::Request;
using assign4::Response;

using assign4::childnode;

std::string child_target;
std::string super_target;
std::string DB_target;


struct word_info
{
    int super;
    int index;
    int start;
    std::string word;
};
class Cache
{
    private:
    int cache_size=50;
    std::list<std::string> l;
    std::unordered_map<std::string, std::string> c;
    std::unordered_map<std::string, std::list<std::string>::iterator> p;
    void use(std::string key)
    {
        if(p.find(key) != p.end())
        {
            l.erase(p[key]);
        }
        else if(l.size() >= cache_size)
        {
            std::string old = l.back();
            l.pop_back();
            c.erase(old);
            p.erase(old);
        }
        l.push_front(key);
        p[key] = l.begin();
    }

    public:
        std::string getvalue(std::string key)
        {
            std::string zero{"\0", 1};
            if(c.find(key) != c.end())
            {
                use(key);
                return c[key];
            }
            return "";
        }
        void setvalue(std::string key, std::string value)
        {
            use(key);
            c[key] = value;
        }
};

Cache word_cache;

class DatabaseClient
{
    public:
        DatabaseClient(std::shared_ptr<Channel> channel)
            : stub_(Database::NewStub(channel)){}

    std::string getvalue(const std::string req)
    {
        Request request;
        request.set_req(req);

        Response response;
        
        ClientContext context;

        Status status;
        status = stub_->AccessDB(&context, request, &response);
        
        //std::cout << req << " : " << response.res() << "\n";
        return response.res();
    }

    private:
        std::unique_ptr <Database::Stub> stub_;
};

class ChildServiceImpl final : public childnode::Service
{
    Status getvalue(ServerContext* context, const Request_word*  request, Response_word* response) override
    {
        std::string word;
        std::string trans_word;
        word = request->word();
        std::string zero{"\0", 1};
        std::string cache_word = word_cache.getvalue(word);
        if(!cache_word.empty())
        {
            //printf("found in cache\n");
            response->set_word(cache_word);
        }
        else
        {
            DatabaseClient DBsearch(grpc::CreateChannel(DB_target, grpc::InsecureChannelCredentials()));
            trans_word = DBsearch.getvalue(word);
            response->set_word(trans_word);
            word_cache.setvalue(word, trans_word);
        }
        response->set_super(request->super());
        response->set_index(request->index());
        response->set_start(request->start());
        return Status::OK;
    }
};

void runserver(std::string server_target)
{
    std::string server_address(server_target);
    ChildServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Child listening on " << server_target << std::endl;

    server->Wait();
}


int main(int argc, char* argv[])
{
    std::string grpc_port = argv[1];
    child_target = "0.0.0.0:"+grpc_port;
    super_target = argv[2];
    DB_target = argv[3];

    runserver(child_target);
}
