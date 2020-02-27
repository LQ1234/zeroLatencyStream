// g++ main.cpp -omain -l NvPipe -I include -lX11 -lXext -Ofast -mfpmath=both -march=native -m64 -funroll-loops -mavx2 `pkg-config opencv --cflags --libs` -g && ./main
//#include <opencv2/opencv.hpp>  // This includes most headers!
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>


#include <time.h>

#include <NvPipe.h>

#include "utils.h"

#include <iostream>
#include <vector>
#include <stdlib.h>
#include <unistd.h>

#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#include <signal.h>
#include <atomic>

struct ScreenShot{
    ScreenShot(uint x, uint y, uint width, uint height):
               x(x), y(y), width(width), height(height){
        display = XOpenDisplay(":1");
        if(display==NULL)printf("Warning: display is null");

        root = DefaultRootWindow(display);
        XGetWindowAttributes(display, root, &window_attributes);
        screen = window_attributes.screen;
        ximg = XShmCreateImage(display, DefaultVisualOfScreen(screen), DefaultDepthOfScreen(screen), ZPixmap, NULL, &shminfo, width, height);

        shminfo.shmid = shmget(IPC_PRIVATE, ximg->bytes_per_line * ximg->height, IPC_CREAT|0777);
        shminfo.shmaddr = ximg->data = (char*)shmat(shminfo.shmid, 0, 0);
        shminfo.readOnly = False;
        if(shminfo.shmid < 0)
            puts("Fatal shminfo error!");;
        Status s1 = XShmAttach(display, &shminfo);
        printf("XShmAttach() %s\n", s1 ? "success!" : "failure!");

        init = true;
    }

    uint8_t* operator() (){
        if(init)
            init = false;

        XShmGetImage(display, root, ximg, 0, 0, 0x00ffffff);
        return(reinterpret_cast<uint8_t*>(ximg->data));
    }

    ~ScreenShot(){
        std::cout<<"Destroying image...\n";
        if(!init)
            XDestroyImage(ximg);
        std::cout<<"Detaching from shared memory...\n";
        XShmDetach(display, &shminfo);
        shmdt(shminfo.shmaddr);
        std::cout<<"Closing display...\n";
        XCloseDisplay(display);
        std::cout<<"Display closed\n";

    }

    Display* display;
    Window root;
    XWindowAttributes window_attributes;
    Screen* screen;
    XImage* ximg;
    XShmSegmentInfo shminfo;

    int x, y, width, height;

    bool init;
};

struct Encoder{
    Encoder(uint32_t w,uint32_t h):width(w),height(h),uncompressed_size(w*h*4){
        const NvPipe_Codec codec = NVPIPE_H264;
        const NvPipe_Compression compression = NVPIPE_LOSSLESS;
        const float bitrateMbps = 100;
        const uint32_t targetFPS = 90;
        std::cout<<"Trying to create encoder ...\n";
        encoder = NvPipe_CreateEncoder(NVPIPE_RGBA32, codec, compression, bitrateMbps * 1000 * 1000, targetFPS, width, height);
        std::cout<<"Encoder created\n";

        compressed_data=new uint8_t[uncompressed_size];
    }
    void operator() (uint8_t* uncompressed_input){
        compressed_size = NvPipe_Encode(encoder, uncompressed_input, width * 4, compressed_data,uncompressed_size, width, height, false);
        if (0 == compressed_size)
            std::cerr << "Encode error: " << NvPipe_GetError(encoder) << "\n";
    }
    ~ Encoder(){
        delete[] compressed_data;
        std::cout<<"Destroying pipe ...\n";
        NvPipe_Destroy(encoder);
        std::cout<<"Destroyed\n";
    }
    uint32_t width;
    uint32_t height;
    uint64_t uncompressed_size;
    uint64_t compressed_size;
    uint8_t* compressed_data;

    NvPipe* encoder;

};

std::atomic<bool> stop(false);

void got_signal(int)
{
    stop.store(true);
}

//based on https://ncona.com/2019/04/building-a-simple-server-with-cpp/
int main(int argc, char* argv[])
{
    { //clean up on control-c
      struct sigaction sa;// why is sigaction both a func and a class
      memset( &sa, 0, sizeof(sa) );
      sa.sa_handler = got_signal;
      sigfillset(&sa.sa_mask);
      sigaction(SIGINT,&sa,NULL);
    }
    //set up screenshooter & encoder
    const uint32_t width = 1280;
    const uint32_t height = 720;
    Encoder encode(width,height);
    ScreenShot screenshot(0, 0, width, height);

    //set up server
    const int port=8000+rand() % 30 ;
    std::cout<<"port "<<port<<"\n";
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
       std::cout << "Failed to create socket. \n";
       exit(1);
    }
    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0) {
        std::cout << "Failed to bind to port\n";
        exit(1);
    }
    if (listen(server_socket, 10) < 0) {
        std::cout << "Failed to listen on socket\n";
        exit(1);
    }
    socklen_t addrlen = sizeof(sockaddr_in);

    int connection = accept(server_socket, reinterpret_cast<sockaddr*>(&server_address), &addrlen);

    Timer timer;

    send(connection,  static_cast<const void*>(&width), sizeof(uint32_t), MSG_NOSIGNAL);
    send(connection,  static_cast<const void*>(&height), sizeof(uint32_t), MSG_NOSIGNAL);

    while(!stop){
        timer.reset();
        uint8_t* shot=screenshot();
        encode(shot);
        double time = timer.getElapsedMilliseconds();
        //std::cout<<"Time taken: "<< std::setprecision(3) << std::setw(8) <<time<<"ms"<< std::setprecision(3) << std::setw(8) <<encode.compressed_size/1000.<<"kb\n";
        uint64_t size=encode.compressed_size;
        if(send(connection,  static_cast<void*>(&size), sizeof(uint64_t), MSG_NOSIGNAL)<0)break;
        if(send(connection, encode.compressed_data,encode.compressed_size, MSG_NOSIGNAL)<0)break;
    }
    std::cout<<"client disconnected\n";
    return 0;
}
