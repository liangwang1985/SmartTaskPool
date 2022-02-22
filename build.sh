#!/bin/bash 

g++ -std=c++11 -fPIC  -I./include -lpthread -g  -o example ./example.cpp
g++ -std=c++11 -fPIC  -I./include -lpthread -g  -o example_mvc ./example_mvc.cpp
# g++ -std=c++11 -fPIC  -I./include -lpthread -O2  -o example ./example.cpp

