# Assignment 2

- Student ID: 20170533
- Your Name: Dohoon LIM
- Submission date and time: 2020.10.17 23:30 (Part1 finished) / 2020.10.27 23:00 (Part2 Part3 finished)

## Ethics Oath
I pledge that I have followed all the ethical rules required by this course (e.g., not browsing the code from a disallowed source, not sharing my own code with others, so on) while carrying out this assignment, and that I will not distribute my code to anyone related to this course even after submission of this assignment. I will assume full responsibility if any violation is found later.

Name: Dohoon LIM
Date: 2020.10.17

## Performance measurements
Set 8kB-sized value into a specific key. Measure the time for running 1,000 concurrent GET requests on the key using `ab -c 1000 -n 1000`.
- Part 1
  - Completed requests: 1000
  - Time taken for test: 126 ms
  - 99%-ile completion time: 96 ms
- Part 2
  - Completed requests: 1000
  - Time taken for test: 171 ms
  - 99%-ile completion time: 142 ms
- Part 3
  - Completed requests: 1000
  - Time taken for test: 345 ms
  - 99%-ile completion time: 296 ms

Briefly compare the performance of part 1 through 3 and explain the results.

The performance is different with my thought. I think event or thread will be faster than fork.
But actually fork was faster. And libevent was slowest. I think I can revise this at HW3 and can 
make it faster. And the varience of time is too big so every time I check time it's difference is about 400-600ms. 
So I thought the accuracy of timer is not that good or server I checked is not that robust. 

## Brief description
At webserver fork I use same algorithm with HW1 to fork and take care of concurrency.
I make server to get http request from client and inside server I made socket to redis server. 
After I get request from client I parse http and made redis request and get redis response.
And after parse redis response I send http response to client and end connection. 

At redis_parse.h I made functions to handle redis request and response. 
I separate set request function and get request function, set response function and get response functon.
And parse, make request, response.

At http_parse.h I made functions to handle http request and response.
I made first line parser and header parser to parse http request.
And for response I made http_ok to send 200 ok response and send_error to send 404 error.
And for get request I made other send 200 ok response with value from redis server.

For thread and libevent cases I search about pthread and libevent but I cannot understand them
properly. So I have many difficulty while coding them. I did similar with fork. After accept connection
I use thread or event to handle them. The big difference is in thread and libevent they use same space to 
store informations. So I have to handle them to remove race condition. But I am not sure I did well.

## Misc
Describe here whatever help (if any) you received from others while doing the assignment.

How difficult was the assignment? (1-5 scale)
5 (too difficult for me)
How long did you take completing? (in hours)
about 150-60 hours

It was too difficult for me. I did my best but I'm not sure I handle concurrent case and robustness case well.
In my server they works well but in other server I saw some segmentation faults... I think my server has 
limit of requests that can handle. I think I can revise them at HW3. So I will do my best at HW3 too. At HW2 I
start HW right after it announced but my programming skill is too poor to did it in time. So I thought I should
study more at HW3. 