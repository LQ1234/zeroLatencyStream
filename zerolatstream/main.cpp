// clang++ -o main main.cpp -lavcodec -lavformat -lavdevice -lavfilter -lavformat -lavutil -framework VideoDecodeAcceleration -framework CoreVideo -framework CoreFoundation -lavresample -lm -lz -g
// clang++ -o main main.cpp -lavcodec -lavformat -lavdevice -lavfilter -lavformat -lavutil -framework VideoDecodeAcceleration -framework CoreVideo -framework CoreFoundation -lavresample -lm -lz -g -I../opengl-libs/glew-2.1.0/include/ -I../opengl-libs/glm -lGLEW -framework OpenGL -lglfw3 -framework Cocoa -framework IOKit -std=c++11
#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <iomanip>
#include <sys/ioctl.h>
#include <chrono>
#include <mutex>

extern "C"{
  #include <libavcodec/avcodec.h>
  //#include <libavdevice/avdevice.h>
  //#include <libavfilter/avfilter.h>
  #include <libavformat/avformat.h>
  #include <libavformat/avio.h>
  //#include <libavutil/avutil.h>
  //#include <libswscale/swscale.h>
}

#include "YUVRenderer.hpp"

#include <thread>

struct Decoder{
  Decoder(uint32_t w,uint32_t h):width(w),height(h){
    avcodec_register_all();
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if (avcodec_open2(c, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    next_frame = av_frame_alloc();
    if (!next_frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    av_init_packet(&next_frame_pkt);
    next_frame_pkt.data=new uint8_t[width*height*4+AV_INPUT_BUFFER_PADDING_SIZE];//compressed must be less or equal to uncompressed
  }


  void create_next_frame(uint64_t size){
    /*
    std::cout<<"timport:"<<(next_frame_pkt.data==NULL?"null":"not null") <<"\n";
    if(size>=width*height*4){
      std::cout<<"oopsie size("<<size<<")>max("<<width*height*4<<")\n";
    }*/
    memset(next_frame_pkt.data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    next_frame_pkt.size=size;
  }
  uint8_t* get_next_frame_pointer(){
    return(next_frame_pkt.data);
  }
  void operator() (){

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged mpeg streams) */
    avcodec_send_packet(c,&next_frame_pkt);
    avcodec_receive_frame(c,next_frame);
  }
  ~Decoder(){
    delete[] next_frame_pkt.data;

    next_frame_pkt.data = NULL;
    next_frame_pkt.size = 0;
    avcodec_close(c);
    av_free(c);
    av_frame_free(&next_frame);
    printf("Dele\n");
  }
  AVCodec *codec;
  AVCodecContext *c= NULL;

  AVFrame *next_frame;
  AVPacket next_frame_pkt;
  uint32_t width;
  uint32_t height;
};

int read_blocking(int sock, void* buf, size_t buf_size){
  int amt_read=0;
  char* buf_point=static_cast<char*> (buf);
  while(amt_read<buf_size){
    int just_read=read( sock, buf_point, buf_size-amt_read);
    if(just_read<0){
      return(just_read);
    }
    amt_read+=just_read;
    buf_point+=just_read;
  }
  return(amt_read);
}
bool start_io( int port,  std::mutex& renderStreamMutex,uint32_t &renderStreamWidth,uint32_t &renderStreamHeight,unsigned char* &renderStreamY,unsigned char* &renderStreamU,unsigned char* &renderStreamV){
    int client_socket=socket(AF_INET, SOCK_STREAM, 0);
    if(client_socket<0){
      std::cout<<"Unable to create socket";
      return true;
    }
    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);


    if(inet_pton(AF_INET, "172.24.1.55", &server_address.sin_addr)<=0)
    {
        printf("\nInvalid address\n");
        return true;
    }

    if (connect(client_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) < 0)
    {
        printf("\nConnection Failed \n");
        return true;
    }

    uint32_t width;
    uint32_t height;

    read_blocking(client_socket,  static_cast<void*>(&width), sizeof(uint32_t));
    read_blocking(client_socket,  static_cast<void*>(&height), sizeof(uint32_t));

    std::cout<<"width:" <<width<<" height: "<<height<<"\n";
    {
        std::lock_guard<std::mutex> guard(renderStreamMutex);

        renderStreamWidth=width;
        renderStreamHeight=height;

        renderStreamY=new unsigned char[width*height];
        renderStreamU=new unsigned char[width*height/4];
        renderStreamV=new unsigned char[width*height/4];
    }
    Decoder decode(width,height);

    uint64_t compressed_size = 0;

    while(true){
      auto t1 = std::chrono::high_resolution_clock::now();

      read_blocking( client_socket , static_cast<void*>(&compressed_size), sizeof(uint64_t));
      decode.create_next_frame(compressed_size);

      read_blocking(client_socket, decode.get_next_frame_pointer(),compressed_size);
      std::cout<<"imp: "<<AV_PIX_FMT_YUV420P<<" "<<decode.next_frame->format<<"\n";
      std::cout<<avpicture_get_size((AVPixelFormat)AV_PIX_FMT_YUV420P,width,height)<<"\n";
      decode();
      {
          std::lock_guard<std::mutex> guard(renderStreamMutex);
          if(decode.next_frame->format==0){
              memcpy(renderStreamY,decode.next_frame->data[0],width*height);
              memcpy(renderStreamU,decode.next_frame->data[1],width*height/4);
              memcpy(renderStreamV,decode.next_frame->data[2],width*height/4);
          }
      }
      //td::cout<<"size: "<<avpicture_get_size((AVPixelFormat)(decode.next_frame->format),decode.next_frame->width,decode.next_frame->height)<<"\n";

      auto t2 = std::chrono::high_resolution_clock::now();

      auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();

      //std::cout<<"time:"<<std::setprecision(3) << std::setw(8) << duration/1000.0<<"ms\n";

    }
    return false;
}
int main(int argc, char const *argv[])
{

    std::mutex renderStreamMutex;

    uint32_t width;
    uint32_t height;
    unsigned char* renderStreamY=NULL;
    unsigned char* renderStreamU=NULL;
    unsigned char* renderStreamV=NULL;
    std::thread iothread(start_io,8024,std::ref(renderStreamMutex),std::ref(width),std::ref(height),std::ref(renderStreamY),std::ref(renderStreamU),std::ref(renderStreamV));
    YUVRenderer::setStreamVars(renderStreamMutex,width,height,renderStreamY,renderStreamU,renderStreamV);
    YUVRenderer::mainLoop();
    return(1);
}
