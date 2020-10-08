FROM registry.cn-hangzhou.aliyuncs.com/test_tc/llvm_hw:0.2
WORKDIR /root/AST_Interpreter/build
RUN apt-get install gdb -y
RUN mkdir /root/test
COPY ./ast-interpreter /root/AST_Interpreter
COPY ./test /root/test
RUN cmake ../.
RUN make -j4 
