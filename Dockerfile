
FROM gcc:11.3 as build

RUN apt update && \
    apt install -y \
      python3-pip \
      cmake \
      libpq-dev \
    && \
    pip3 install conan==1.64.0

WORKDIR /app

COPY conanfile.txt .
RUN mkdir build && cd build && \
    conan install .. --build=missing -s compiler.libcxx=libstdc++11 -s build_type=Release

COPY ./src ./src
COPY ./tests ./tests 
COPY CMakeLists.txt .

RUN cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    cmake --build . --config Release

FROM ubuntu:22.04 as run

RUN apt update && \
    apt install -y libpq5 && \
    rm -rf /var/lib/apt/lists/*

RUN mkdir -p /var/log && chmod 777 /var/log

RUN groupadd -r www && useradd -r -g www www
USER www

WORKDIR /app

COPY --from=build /app/build/game_server .
COPY ./data ./data
COPY ./static ./static 

ENV GAME_DB_URL=postgresql://user:pass@db:5432/game_db

ENTRYPOINT ["/app/game_server", "-c", "/app/data/config.json", "-w", "/app/static"]