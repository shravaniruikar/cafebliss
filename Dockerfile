FROM gcc:13
WORKDIR /app
COPY . .
RUN test -f libraries/cpp-httplib/httplib.h
RUN g++ -std=c++17 -pthread -I/app/libraries/cpp-httplib backend/main.cpp -o cafe_server
CMD ["./cafe_server"]