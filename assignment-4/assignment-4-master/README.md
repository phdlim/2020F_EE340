# Assignment 4

- Student ID: 20170533
- Your Name: Dohoon LIM
- Submission date and time: 2020-12-21 23:40

## Ethics Oath
I pledge that I have followed all the ethical rules required by this course (e.g., not browsing the code from a disallowed source, not sharing my own code with others, so on) while carrying out this assignment, and that I will not distribute my code to anyone related to this course even after submission of this assignment. I will assume full responsibility if any violation is found later.

Name: Dohoon LIM
Date: 2020.12.21

## Brief description

1. Supernode
First supernode get message from client. And give half of them to second one. And supernode parse message to words and non-words. 
The supernode made queue of each child node and put words in child node equally. Then dequeue word and send child node and translate word.
After that child node send translated word or send miss. If it send word then put it in supernode cache. And if miss is sent and the word is not
in cache the word is sent to next child node's queue. If all child send miss for one word it sent to another supernode and put miss in cache. 
Then the other supernode do same thing and send translated word to supernode. Then it put word in cache. And if two supernode translate all the words.
then it reconstruct message again and send it to client.

2. Child node
The supernode send word to child node. If the word is not in child node then the child node send word to DB server. And DB server search 
word in DB and send hit or miss to child. And child put it in it's own cache and send result to supernode. And if the word is in cache then
the child node search cache and send it to supernode.

3. Client node
client node send message to first supernode and get translated message. And make file.

4. Cache
Cache save word and reduce time to search in other nodes. Cache for supernode saves hit of child nodes and miss of child nodes. And the 
word translation from other supernode.

* P.S It runs on durian4 testing machine. But at eelab5 it does not compile. So please consider this and run codes on durian4 server. Thank you.
* And the supernode terminates after do task only once. 
* the client send request to only first supernode(without -s) and first supernode get request and start translation. As it said in assignment documentation.

# Misc
Describe here whatever help (if any) you received from others while doing the assignment.

How difficult was the assignment? (1-5 scale)
5

How long did you take completing? (in hours)
over 100hrs