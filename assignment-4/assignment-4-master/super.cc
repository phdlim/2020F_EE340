#include <iostream>
#include <string>
#include <thread>
#include <future>
#include <ctype.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <grpc++/grpc++.h>
#include "assign4.grpc.pb.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <list>
#include <queue>
#include <mutex>

using grpc::Channel;
using grpc::Status;
using grpc::ClientContext;
using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;

using assign4::Request;
using assign4::Response;
using assign4::Request_word;
using assign4::Response_word;
using assign4::supernode;
using assign4::childnode;

void task(char* buf);

std::string super_target;
int super_set = 0;
char* buf  = (char*)malloc(4*1024*1025);
int flag=0;
struct word_info
{
    int super;
    int index;
    int start;
    std::string word;
};


std::list<std::string> non_word;
std::list<std::string> word;
std::queue<word_info> q[10];
std::queue<word_info> super;
std::string child[10];
int child_num;
int translate_size;
int translate_size_child[10];
int translate_size_super;
int ran=0;
std::mutex queue_mutex[10];
std::mutex cache_mutex;
std::string result2;

std::string words[4000000];
std::string translated_word[4000000];

/* class for caching */
class Cache
{
    private:
    static const constexpr int cache_size=150;
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

/* rpc of supernode server */
class SupernodeServiceImpl final : public supernode::Service
{
    Status line_req(ServerContext* context, const Request* request, Response* response) override
    {
        const char *buffer;
        //printf("a\n");
        buffer = request->req().c_str();
        //std::cout << request->req() << "\n";
        strcpy(buf, buffer);
        std::thread task_thread(task, buf);
        task_thread.detach();
        //printf("%s\n", buf);
        return Status::OK;
    }
    Status word_req(ServerContext* context, const Request_word* request, Response_word* response) override
    {
        word_info req_w;
        req_w.start = ran%child_num;
        req_w.super = request->super();
        req_w.index = request->index();
        req_w.word = request->word();
        //std::cout << req_w.word << " sent\n";
        queue_mutex[ran%child_num].lock();
        q[ran%child_num].push(req_w);
        queue_mutex[ran%child_num].unlock();
        ran++;
        return Status::OK; 
    }

    Status word_res(ServerContext* context, const Request_word* request, Response_word* response) override
    {
        word_info res_w;
        res_w.start = request->start();
        res_w.super = request->super();
        res_w.index = request->index();
        res_w.word = request->word();
        std::string zero{"\0", 1};
        if(res_w.word.compare(zero))
        {
            
            translated_word[res_w.index] = res_w.word;
            translate_size_super++;
            cache_mutex.lock();
            //printf("cache\n");
            word_cache.setvalue(words[res_w.index], res_w.word);
            cache_mutex.unlock();
            //std::cout << "get " << res_w.word << "\n";
            //printf("left: %d\n", translate_size);
            //printf("5\n");
            return Status::OK; 
        }
        return Status::OK;
    }

    Status get_result(ServerContext* context, const Request* request, Response* response) override
    {
        result2 = request->req();
        return Status::OK;
    }
    Status connect(ServerContext* context, const Request* request, Response* response) override
    {
        super_target = request->req();
        super_set=1;
        //std::cout << "Server get " << super_target << "\n";
        return Status::OK;
    }
};

/* grpc between two supernodes */
class SupernodeClient
{
    public:
        SupernodeClient(std::shared_ptr<Channel> channel)
            : stub_(supernode::NewStub(channel)) {}
    
    void sendtarget(const std::string target)
    {
        Request request;
        request.set_req(target);

        Status status;

        ClientContext context;

        Response response;

        status = stub_->connect(&context, request, &response);
        if(status.ok())
        {
            printf("supernode connection success\n");
        }
    }

    void send_string(std::string input)
    {
        Request request;
        request.set_req(input);
        Status status;
        
        ClientContext context; 

        Response response;

        status = stub_->line_req(&context, request, &response);
        //if(status.ok())
        //{
            //printf("send string\n");
        //}
        //else
        //{
            //printf("cannot send string\n");
        //}
    }

    void send_word(word_info req_w)
    {
        Request_word request;
        request.set_super(req_w.super);
        request.set_index(req_w.index);
        request.set_start(req_w.start);
        request.set_word(req_w.word);

        ClientContext context;
        Status status;

        Response_word response;

        status = stub_->word_req(&context, request, &response);
        
    }

    void trans_word(word_info req_w)
    {
        Request_word request;
        request.set_super(req_w.super);
        request.set_index(req_w.index);
        request.set_start(req_w.start);
        request.set_word(req_w.word);
        //std::cout << req_w.word << " will be send\n";
        Status status;

        ClientContext context;

        Response_word response;

        status = stub_->word_res(&context, request, &response);
    }

    void trans_string(std::string result)
    {
        Request request;
        request.set_req(result);

        Status status;

        ClientContext context;

        Response response;

        status = stub_->get_result(&context, request, &response);
    }
    private:
        std::unique_ptr<supernode::Stub> stub_;
};

/* grpc client with child node */
class ChildClient
{
    public:
        ChildClient(std::shared_ptr<Channel> channel)
            : stub_(childnode::NewStub(channel)) {}
    word_info getvalue(word_info req_w)
    {
        Request_word req_word;
        req_word.set_super(req_w.super);
        req_word.set_index(req_w.index);
        req_word.set_word(req_w.word);
        Status status;

        ClientContext context;

        Response_word res_word;

        status = stub_->getvalue(&context, req_word, &res_word);

        word_info res_w;
        res_w.super = res_word.super();
        res_w.index = res_word.index();
        res_w.word = res_word.word();
        res_w.start = res_word.start();
        return res_w;
    }

    private:
        std::unique_ptr<childnode::Stub> stub_;
};

/* supernode server */
void runserver(std::string server_target)
{
    std::string server_address(server_target);
    SupernodeServiceImpl service;

    ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_target << std::endl;

    server->Wait();
}

std::mutex super_mutex;

/* function communicate with child node */
void child_word(int i)
{
    word_info req_w;
    word_info res_w;
    std::string word;
    int super_num;
    int index_num;
    ChildClient childcli(grpc::CreateChannel(child[i], grpc::InsecureChannelCredentials()));
    SupernodeClient supercli(grpc::CreateChannel(super_target, grpc::InsecureChannelCredentials()));
    while(1)
    {   
        queue_mutex[i].lock();
        int check_empty = q[i].empty();
        queue_mutex[i].unlock();
        if(!check_empty)
        {
            queue_mutex[i].lock();
            req_w = q[i].front();
            q[i].pop();
            queue_mutex[i].unlock();
            res_w = childcli.getvalue(req_w);
            //std::cout << req_w.word << " " << i << " " << res_w.word << res_w.word.size() << "\n";
            word = res_w.word;
            std::string w = req_w.word;
            std::string cache_ans;
            //std::cout << w << "\n"; 
            std::string zero{"\0", 1};

            if(!word.compare(zero))
            {
                if(req_w.start == (i+1)%child_num && req_w.super == flag+1)
                {
                    //printf("1\n");
                    cache_mutex.lock();
                    word_cache.setvalue(w, word);
                    cache_mutex.unlock();
                    super_mutex.lock();
                    super.push(req_w);
                    super_mutex.unlock();
                }
                else if(req_w.start == (i+1)%child_num)
                {
                    //printf("2\n");
                    //super.push(req_w);
                    //supercli.trans_word(res_w);
                }
                else
                {
                    //printf("cac\n");
                    cache_mutex.lock();
                    cache_ans = word_cache.getvalue(w);
                    cache_mutex.unlock();
                    if(!cache_ans.empty())
                    {
                        //printf("not empty\n");
                        if(!cache_ans.compare(zero))
                        {
                            super_mutex.lock();
                            super.push(req_w);
                            super_mutex.unlock();
                        }
                        else
                        {
                            if(req_w.super == flag+1)
                            {
                                translated_word[req_w.index] = cache_ans;
                                translate_size_child[i]++;
                            }
                            else
                            {
                                res_w.word = cache_ans;
                                supercli.trans_word(res_w);
                                cache_mutex.lock();
                                word_cache.setvalue(w, word);
                                cache_mutex.unlock();
                            }
                        }
                    }
                    else
                    {
                        queue_mutex[(i+1)%child_num].lock();
                        q[(i+1)%child_num].push(req_w);
                        queue_mutex[(i+1)%child_num].unlock();
                        //std::cout << "move to another child node\n" << "\n";
                    }
                }
            }
            else if(req_w.super==flag+1)
            {
                //printf("4\n");
                translated_word[req_w.index] = res_w.word;
                translate_size_child[i]++;
                //printf("left:%d\n", translate_size);
                cache_mutex.lock();
                word_cache.setvalue(req_w.word, res_w.word);
                //printf("cache\n");
                cache_mutex.unlock();
            }
            else if(req_w.super!=flag+1)
            {
                supercli.trans_word(res_w);
                cache_mutex.lock();
                word_cache.setvalue(w, word);
                cache_mutex.unlock();
            }
        }
    }
}

/* function communicate with other super node */
void super_word()
{
    word_info req_w;
    word_info res_w;
    SupernodeClient supercli(grpc::CreateChannel(super_target, grpc::InsecureChannelCredentials()));
    while(1)
    {
        super_mutex.lock();
        if(!super.empty())
        {
            req_w = super.front();
            super.pop();
            supercli.send_word(req_w);
            //std::cout << "send : " << req_w.word << "\n";
        }
        super_mutex.unlock();
    }    
}


int main(int argc, char* argv[])
{
    std::string grpc_port;

  
    if(argc < 3)
    {
        printf("No ports\n");
        return -1;
    }  
    else if(argc < 4)
    {
        printf("No childs\n");
        return -1;
    }

    if(!strcmp(argv[3], "-s"))
    {
        printf("second supernode\n");
        flag = 1;
    }
    // flag = 0 : first super node
    // flag = 1 : second super node
    if(flag == 0)
    {
        grpc_port = argv[2];
        std::string server_target = "0.0.0.0:"+grpc_port;
        //runserveronce(server_target);
        std::thread server_thread(runserver, server_target);
        server_thread.detach();
        while(!super_set){}
        std::cout << super_target << "\n";
    }

    if(flag == 1)
    {
        grpc_port = argv[2];
        super_target = argv[4];
        SupernodeClient supernode(grpc::CreateChannel(super_target, grpc::InsecureChannelCredentials()));
        struct ifreq ifr;
        int fd;
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        ifr.ifr_addr.sa_family = AF_INET;
        char ip_address[15];
        char iface[] = "kaist"; // durian machine
        //char iface[] = "enp0s31f6" //eelab machine
        memcpy(ifr.ifr_name, iface, IFNAMSIZ-1);
        ioctl(fd, SIOCGIFADDR, &ifr);
        close(fd);
        strcpy(ip_address, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
        printf("IP address: %s\n", ip_address);
        std::string ip(reinterpret_cast<char const *>(ip_address));
        std::string server_target = ip+":"+grpc_port;
        server_target = "0.0.0.0:"+grpc_port;
        supernode.sendtarget(server_target);
        std::thread server_thread(runserver, server_target);
        server_thread.detach();
    }


    if(flag == 0)
    {
        child_num = argc-3;
        std::cout << "child number " << child_num << "\n";
        for(int i=0;i<child_num;i++)
        {
            child[i] = argv[3+i];
        }
    }

    if(flag == 1)
    {
        child_num = argc-5;
        for(int i=0;i<child_num;i++)
        {
            child[i] = argv[5+i];
        }
    }

    std::thread child_thread[child_num];
    std::thread super_thread;
    for(int i=0;i<child_num;i++)
    {
        child_thread[i] = std::thread(child_word, i);
        child_thread[i].detach();
    }

    super_thread = std::thread(super_word);
    super_thread.detach();

    struct sockaddr_in clientaddr;
    struct sockaddr_in serveraddr;
    int server_socket;
    int client_socket;
    socklen_t clientlen; 
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
	  if(server_socket < 0)
	  {
		    printf("Create Socket Error\n");
		    return -1;
	  }
    int optval=1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int))<0)
        return -1;
    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, (const void *)&optval, sizeof(int))<0)
        return -1;


    memset(&serveraddr, 0, sizeof(serveraddr));
	  serveraddr.sin_family = AF_INET;
	  serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	  serveraddr.sin_port = htons(atoi(argv[1]));

    if(bind(server_socket, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0)
	  {
		    printf("bind ERROR\n");
		    return -1;
	  }
	
	  /* Listen socket */
	  if(listen(server_socket, 10) < 0)
	  {
		    printf("listen ERROR\n");
		    return -1;
	  }

    printf("Listening %s\n", argv[1]);
    while(1)
    {   
        printf("1\n");
        clientlen = sizeof(clientaddr);
        client_socket = accept(server_socket, (struct sockaddr *)&clientaddr, &clientlen);
		    if(client_socket < 0)
		    {
            perror("accept ERROR");
			      printf("accept ERROR\n");
			      return -1;
		    }
        int size=0;
        int length;
        char l[20];
        //char* buf = (char*)malloc(4*1024*1025);
        while(read(client_socket, (l+size), 1))
        {
            if(*(l+size) == ' ')
            {
                l[size] = '\0';
                break;
            }
            size++;
        }
        length = atoi(l);
        size=0;
        while(read(client_socket, (buf+size), 1))
        {
            size++;
            if(size==length)
                break;
        }
        int half_size = length/2;
        while(isalnum(buf[half_size]) && half_size != size)
        {
            //printf("%c\n", buf[half_size]);
            half_size++; 
        }

        char* send_buf = (char *)malloc(4*1024*1025);
        strcpy(send_buf, (buf+half_size));
        std::string str(send_buf);
        //std::cout << super_target << "\n";
        SupernodeClient super(grpc::CreateChannel(super_target, grpc::InsecureChannelCredentials()));
        super.send_string(send_buf);
        free(send_buf);

            std::string result;
            std::string w;
            std::string nw;

            int state=0;


            if(isalnum(buf[0]))
            {   
                non_word.push_back("");
                state=1;
            }
            for(int i=0;i<half_size;i++)
            {
                if(state==1 && isalnum(buf[i]))
                {
                    w.push_back(buf[i]);
                }
                else if(state==1 && !isalnum(buf[i]))
                {
                    word.push_back(w);
                    w.clear();
                    nw.push_back(buf[i]);
                    state=0;
                }
                else if(state==0 && !isalnum(buf[i]))
                {
                    nw.push_back(buf[i]);
                }
                else if(state==0 && isalnum(buf[i]))
                {
                    non_word.push_back(nw);
                    nw.clear();
                    w.push_back(buf[i]);
                    state=1;
                }
            }
            if(state==0)
            {
                non_word.push_back(nw);
                nw.clear();
            }
            else if(state==1)
            {
                word.push_back(w);
                w.clear();
            } 
            std::list<std::string>::iterator it;

            //for(it=word.begin();it!=word.end();it++)
            //    std::cout << *it << "\n";
            int word_size = word.size();
            int cnt=0;
            word_info req_w;
            for(it=word.begin();it!=word.end();it++)
            {
                req_w.word = *it;
                req_w.super = flag+1;
                req_w.index = cnt;
                req_w.start = cnt%child_num;
                q[cnt%child_num].push(req_w);
                cnt++;
            }

        
            translate_size = word.size();
            //printf("size:%d\n", translate_size); 
            int translate_sum=0;
            //while(translate_sum!=translate_size)
            //{
            //    translate_sum=0;
            //    for(int i=0;i<translate_size;i++)
            //        translate_sum+=translated_check[i];
            //}
            while(translate_size!=translate_sum)
            {   
                translate_sum=0;
                for(int i=0;i<child_num;i++)
                  translate_sum+=translate_size_child[i];
                translate_sum+=translate_size_super;
            }
            //cnt=0;
            //for(it=word.begin();it!=word.end();it++)
            //{
            //    std::cout << *it << " " << translated_word[cnt] << "\n";
            //    cnt++;
            //}
            cnt=0;
            result.clear();
            for(it=non_word.begin();it!=non_word.end();it++)
            {
                result.append(*it);
                result.append(translated_word[cnt]);
                cnt++;
            }
            //std::cout << result << "\n";
            while(result2.empty()) {}

            result=result+result2;
            std::cout << result << "\n";
            const char* send_result = result.c_str();
            char content_header[20];
            sprintf(content_header, "%ld ", strlen(send_result));
            send(client_socket, content_header, strlen(content_header), 0);
            send(client_socket, send_result, strlen(send_result), 0);
            for(int i=0;i<10;i++)
                translate_size_child[i]=0;
            translate_size_super=0;
            result2.clear();
            word.clear();
            non_word.clear();
            memset(buf, 0, sizeof(buf));
        }

    }


    
void task(char* buf)
{
                std::string result;
                std::string w;
                std::string nw;

                int state=0;


                if(isalnum(buf[0]))
                {   
                    non_word.push_back("");
                    state=1;
                }
                for(int i=0;i<strlen(buf);i++)
                {
                    if(state==1 && isalnum(buf[i]))
                    {
                        w.push_back(buf[i]);
                    }
                    else if(state==1 && !isalnum(buf[i]))
                    {
                        word.push_back(w);
                        w.clear();
                        nw.push_back(buf[i]);
                        state=0;
                    }
                    else if(state==0 && !isalnum(buf[i]))
                    {
                        nw.push_back(buf[i]);
                    }
                    else if(state==0 && isalnum(buf[i]))
                    {
                        non_word.push_back(nw);
                        nw.clear();
                        w.push_back(buf[i]);
                        state=1;
                    }
                }
                if(state==0)
                {
                    non_word.push_back(nw);
                    nw.clear();
                }
                else if(state==1)
                {
                    word.push_back(w);
                    w.clear();
                } 
                
                std::list<std::string>::iterator it;

                //for(it=word.begin();it!=word.end();it++)
                //   std::cout << *it << "\n";
                int word_size = word.size();
                int cnt=0;
                word_info req_w;
                for(it=word.begin();it!=word.end();it++)
                {
                    req_w.word = *it;
                    req_w.super = flag+1;
                    req_w.index = cnt;
                    req_w.start = cnt%child_num;
                    q[cnt%child_num].push(req_w);
                    words[cnt] = *it;
                    cnt++;
                }

                translate_size = word.size();

                int translate_sum=0;
                //while(translate_sum!=translate_size)
                //{
                //    translate_sum=0;
                //    for(int i=0;i<translate_size;i++)
                //    translate_sum+=translated_check[i];
                //}
                while(translate_size!=translate_sum)
                {
                    translate_sum=0;
                    for(int i=0;i<child_num;i++)
                      translate_sum+=translate_size_child[i];
                    translate_sum+=translate_size_super;
                    //printf("%d\n", translate_size-translate_sum);
                }

                //printf("Translate Ends\n");
                //cnt=0;
                //for(it=word.begin();it!=word.end();it++)
                //{
                //    std::cout << *it << " " << translated_word[cnt] << "\n";
                //    cnt++;
                //}
                cnt=0;
                result.clear();
                for(it=non_word.begin();it!=non_word.end();it++)
                {
                    result.append(*it);
                    result.append(translated_word[cnt]);
                    cnt++;
                }
                //std::cout << result << "\n";
                SupernodeClient supernode(grpc::CreateChannel(super_target, grpc::InsecureChannelCredentials()));
                supernode.trans_string(result);
                result.clear();
                for(int i=0;i<10;i++)
                    translate_size_child[i]=0;
                translate_size_super=0;
                word.clear();
                non_word.clear();
                memset(buf, 0, sizeof(buf));
}
