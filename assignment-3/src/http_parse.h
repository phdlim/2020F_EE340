int http_read_request(int sock_id, char* method, char* target, int* encode);
int http_parse_first_line(char* line, char* method, char* target);
int http_parse_header_line(char* line, char *name, char *value);
int send_error(int sock_id, int number);
int http_set_ok(int sock_id);
int http_get_response(int sock_id, int length, char* value); 
