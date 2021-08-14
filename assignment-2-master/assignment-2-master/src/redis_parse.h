int redis_server(char *ip, char *port);
int redis_set_request(int sock_id, char* key, char* value, int key_size, int value_size);
int redis_set_response(int sock_id);
int redis_get_request(int sock_id, char* key, int size);
int redis_get_response(int sock_id, char* buf);
int close_redis_server(int sock_id);
