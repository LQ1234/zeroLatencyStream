//clang++ main.cpp -o main -I../opengl-libs/glew-2.1.0/include/ -I../opengl-libs/glm -lGLEW -framework OpenGL -lglfw3 -framework Cocoa -framework IOKit -std=c++11&& ./main
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <GL/glew.h>
#include <vector>

#include <glfw/glfw3.h>

#include <glm/glm.hpp>
using namespace glm;

namespace YUVRenderer{
    GLuint compileShader(char const* shaderSource, GLenum shaderType){
        GLuint shaderID = glCreateShader(shaderType);
        glShaderSource(shaderID, 1, &shaderSource , NULL);
        glCompileShader(shaderID);

        int infoLogLen;
        glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &infoLogLen);
        if ( infoLogLen > 0 ){
            std::vector<char> infoLog(infoLogLen+1);
            glGetShaderInfoLog(shaderID, infoLogLen, NULL, &infoLog[0]);
            std::cout<<"Error compiling the "<<(shaderType==GL_VERTEX_SHADER?"Vertex Shader":"Fragment Shader")<<":\n"<<&(infoLog[0])<<"\n";
        }
        return(shaderID);
    }

    GLuint linkProgram(char const* vertexSource,char const* fragmentSource){
        GLuint vertexShaderID = compileShader(vertexSource,GL_VERTEX_SHADER);
        GLuint fragmentShaderID = compileShader(fragmentSource,GL_FRAGMENT_SHADER);

        GLuint programID = glCreateProgram();
        glAttachShader(programID, vertexShaderID);
        glAttachShader(programID, fragmentShaderID);
        glLinkProgram(programID);

        int infoLogLen;

        glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &infoLogLen);
        if ( infoLogLen > 0 ){
            std::vector<char> infoLog(infoLogLen+1);
            glGetProgramInfoLog(programID, infoLogLen, NULL, &infoLog[0]);
            std::cout<<"Error linking program: \n"<<&(infoLog[0])<<"\n";
        }
        glDetachShader(programID, vertexShaderID);
        glDetachShader(programID, fragmentShaderID);

        glDeleteShader(vertexShaderID);
        glDeleteShader(fragmentShaderID);

        return programID;
    }

    char const * vertexSource = R"""(
    #version 330 core
    in vec2 position;
    uniform vec2 inputsize;
    uniform vec2 screensize;

    out vec2 pixelpos;

    void main(){
      gl_Position.xyz = vec3(position*2.0-1.0,0.0);
      vec2 resizeRat=inputsize/screensize;
      if(resizeRat.x>resizeRat.y){
        resizeRat/=resizeRat.x;
      }else{
        resizeRat/=resizeRat.y;
      }
      gl_Position.xy=(position-vec2(.5,.5))*resizeRat*2.0;
      gl_Position.z=0.0;
      gl_Position.w = 1.0;
      pixelpos=position*vec2(1,-1)+vec2(0,1);

    }
    )""";

    char const * shaderSource = R"""(
    #version 330 core
    out vec3 color;
    in vec2 pixelpos;
    uniform sampler2D YTexture;
    uniform sampler2D UTexture;
    uniform sampler2D VTexture;
    void main(){
        float y=texture(YTexture, pixelpos).r;
        float r=texture(UTexture, pixelpos).r;
        float b=texture(VTexture, pixelpos).r;
        /*
        float c=y-0.0625;
        float d=u-.5;
        float e=v-.5;
        color = vec3(1.1640625*c+1.59765625*e+.5,1.1640625*c-0.390625*d-0.8125*e+.5,1.1640625*c+2.015625*d+.5);
        */
        color=vec3(y+1.402*(r-.5),y-.344*(b-.5)-.714*(r-.5),y+1.772*(b-.5));
    }
    )""";
    GLuint createTexture(unsigned int width,unsigned int height,unsigned char* data){
        GLuint textureID;
        glGenTextures(1, &textureID);

        // "Bind" the newly created texture : all future texture functions will modify this texture
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Give the image to OpenGL
        glTexImage2D(GL_TEXTURE_2D, 0,GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        return(textureID);
    }

    void updateTexture(GLuint textureID,int width,int height,unsigned char* data){
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RED,GL_UNSIGNED_BYTE,data);
    }
    void deleteTexture(GLuint tex){
        glDeleteTextures(1, &tex);

    }
    void createYUVTextures(unsigned int width,unsigned int height,unsigned char* Ydat,unsigned char* Udat,unsigned char* Vdat,GLuint& storeY,GLuint& storeU,GLuint& storeV){
        storeY=createTexture(width,height,Ydat);
        storeU=createTexture(width/2,height/2,Udat);
        storeV=createTexture(width/2,height/2,Vdat);
    }
    void updateYUVTextures(unsigned int width,unsigned int height,unsigned char* Ydat,unsigned char* Udat,unsigned char* Vdat,GLuint& storeY,GLuint& storeU,GLuint& storeV){
        updateTexture(storeY,width,height,Ydat);
        updateTexture(storeU,width/2,height/2,Udat);
        updateTexture(storeV,width/2,height/2,Vdat);
    }
    void deleteYUVTextures(GLuint& storeY,GLuint& storeU,GLuint& storeV){
        deleteTexture(storeY);
        deleteTexture(storeU);
        deleteTexture(storeV);
    }


    GLuint programID;
    GLFWwindow* window;


    static void rect(GLfloat* buf,GLfloat x1,GLfloat y1,GLfloat x2,GLfloat y2){
        buf[ 0]=x1; buf[ 1]=y1;
        buf[ 2]=x1; buf[ 3]=y2;
        buf[ 4]=x2; buf[ 5]=y1;
        buf[ 6]=x2; buf[ 7]=y2;
        buf[ 8]=x1; buf[ 9]=y2;
        buf[10]=x2; buf[11]=y1;
    }

    std::mutex *renderStreamMutex;
    uint32_t *renderStreamWidth=NULL;
    uint32_t *renderStreamHeight=NULL;
    uint32_t lastStreamWidth=0;
    uint32_t lastStreamHeight=0;
    unsigned char* *renderStreamY;
    unsigned char* *renderStreamU;
    unsigned char* *renderStreamV;
    GLuint YTexture,UTexture,VTexture;

    void updateStream(){
        if(renderStreamWidth!=NULL&&renderStreamHeight!=NULL){
            if(lastStreamWidth==0&&lastStreamHeight==0){
                std::cout<<"gen\n";
                createYUVTextures(*renderStreamWidth,*renderStreamHeight,*renderStreamY,*renderStreamU,*renderStreamV,YTexture,UTexture,VTexture);
                lastStreamWidth=*renderStreamWidth;
                lastStreamHeight=*renderStreamHeight;

                glUniform1i(glGetUniformLocation(programID, "YTexture"), 0);
                glUniform1i(glGetUniformLocation(programID, "UTexture"), 2);
                glUniform1i(glGetUniformLocation(programID, "VTexture"), 4);

                glActiveTexture(GL_TEXTURE0 + 0);
                glBindTexture(GL_TEXTURE_2D, YTexture);

                glActiveTexture(GL_TEXTURE0 + 2);
                glBindTexture(GL_TEXTURE_2D, UTexture);

                glActiveTexture(GL_TEXTURE0 + 4);
                glBindTexture(GL_TEXTURE_2D, VTexture);

                glUniform2f(glGetUniformLocation(programID,"inputsize"),*renderStreamWidth,*renderStreamHeight);


            }else if ((*renderStreamWidth)==lastStreamWidth&&(*renderStreamHeight)==lastStreamHeight){
                updateYUVTextures(*renderStreamWidth,*renderStreamHeight,*renderStreamY,*renderStreamU,*renderStreamV,YTexture,UTexture,VTexture);

            }else{
                deleteYUVTextures(YTexture,UTexture,VTexture);
                createYUVTextures(*renderStreamWidth,*renderStreamHeight,*renderStreamY,*renderStreamU,*renderStreamV,YTexture,UTexture,VTexture);
                lastStreamWidth=*renderStreamWidth;
                lastStreamHeight=*renderStreamHeight;
                glUniform2f(glGetUniformLocation(programID,"inputsize"),*renderStreamWidth,*renderStreamHeight);

            }
        }else if(lastStreamWidth!=0||lastStreamHeight!=0){
            deleteYUVTextures(YTexture,UTexture,VTexture);
            lastStreamWidth=0;
            lastStreamHeight=0;
        }
    }
    void render(){
        updateStream();
        glClear( GL_COLOR_BUFFER_BIT );
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glfwSwapBuffers(window);
    }

    void setStreamVars(std::mutex& rsm,uint32_t &rsw,uint32_t &rsh,unsigned char* &rsy,unsigned char* &rsu,unsigned char* &rsv){
        std::cout<<"set vars\n";
        renderStreamMutex=&rsm;
        renderStreamWidth=&rsw;
        renderStreamHeight=&rsh;
        renderStreamY=&rsy;
        renderStreamU=&rsu;
        renderStreamV=&rsv;
    }
    void onResizeCB(GLFWwindow* window, int width, int height)
    {
      glUniform2f(glGetUniformLocation(programID,"screensize"),width,height);
      render();
      glViewport(0, 0, width, height);
    }
    int mainLoop(){
        if( !glfwInit() ) {
            std::cout<< "Failed to initialize GLFW\n";
            return -1;
        }

        glfwWindowHint(GLFW_SAMPLES, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        window = glfwCreateWindow( 1024, 768, "Stream", NULL, NULL);
        if( window == NULL ){
            std::cout<< "Failed to open GLFW window.\n";
            glfwTerminate();
            return -1;
        }
        glfwSetWindowSizeLimits(window, 150, 150, GLFW_DONT_CARE, GLFW_DONT_CARE);

        glfwMakeContextCurrent(window);

        if (glewInit() != GLEW_OK) {
            std::cout<< "Failed to initialize GLEW\n";
            glfwTerminate();
            return -1;
        }

        glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);

        programID = linkProgram(vertexSource,shaderSource);
        glUseProgram(programID);

        glClearColor(0.1f, 0.1f, 0.2f, 0.0f);


        GLfloat vertexBufferRect[12];
        rect(vertexBufferRect,0,0,1,1);
        GLuint vertexbuffer;
        glGenBuffers(1, &vertexbuffer);
        glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertexBufferRect), vertexBufferRect, GL_STATIC_DRAW);


        GLuint VertexArrayID;
        glGenVertexArrays(1, &VertexArrayID);
        glBindVertexArray(VertexArrayID);

        glBindBuffer(GL_ARRAY_BUFFER, vertexbuffer);

        glVertexAttribPointer(
        glGetAttribLocation(programID,"position"),
            2,                  // size
            GL_FLOAT,           // type
            GL_FALSE,           // normalized?
            0,                  // stride
            (void*)0            // array buffer offset
        );

        glEnableVertexAttribArray(glGetAttribLocation(programID,"position"));

        glfwSetFramebufferSizeCallback(window,onResizeCB);

        {
            int width, height;
            glfwGetFramebufferSize(window,&width, &height);
            onResizeCB(window,width,height);
        }


        do{
            render();
            glfwPollEvents();
        }
        while( glfwGetKey(window, GLFW_KEY_ESCAPE ) != GLFW_PRESS &&
            glfwWindowShouldClose(window) == 0 );

        glDisableVertexAttribArray(glGetAttribLocation(programID,"position"));

        glfwTerminate();
        return 0;
    }
};
