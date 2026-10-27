FROM gcc:13
WORKDIR /app
COPY . .
RUN g++ -std=c++17 -pthread -I/app/third_party/cpp-httplib backend/main.cpp -o cafe_server
CMD ["./cafe_server"]