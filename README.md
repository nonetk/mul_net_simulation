# mul_thread_network
This is a use mut_thread simulated network

This is an unrefined version.

You can compile and run it directly with the following parameters:
                    g++ -std=c++11 code.cpp -lpthread -o network -O0 

It does not automatically discover an IP address of the network controller using Sdn.

When you run it, you will enter the controller shell.

You can use display flow, exit, tcp x, ssh x commands in the shell.

On other devices, you can use the display IDX, display flow, exit commands.
