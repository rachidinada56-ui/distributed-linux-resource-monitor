CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Iserver/include
LDFLAGS_AGENT  =

SQLITE_DEV := $(shell ldconfig -p 2>/dev/null | grep -q ' libsqlite3\.so ' && echo yes)
ifeq ($(SQLITE_DEV),yes)
    SQLITE_LINK := -lsqlite3
else
    SQLITE_LINK := $(shell ldconfig -p 2>/dev/null | grep libsqlite3.so | awk '{print $$NF}' | head -1)
endif

LDFLAGS_SERVER = -lpthread $(SQLITE_LINK)

SERVER_SRC = server/src/main.c server/src/queue.c server/src/db.c \
             server/src/json_parse.c server/src/tcp_server.c \
             server/src/http_server.c server/src/worker.c
SERVER_BIN = monitor_server

AGENT_SRC = agent/agent.c
AGENT_BIN = monitor_agent

.PHONY: all clean run-server run-agent

all: $(SERVER_BIN) $(AGENT_BIN)

$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $(SERVER_SRC) $(LDFLAGS_SERVER)

$(AGENT_BIN): $(AGENT_SRC)
	$(CC) $(CFLAGS) -o $@ $(AGENT_SRC) $(LDFLAGS_AGENT)

clean:
	rm -f $(SERVER_BIN) $(AGENT_BIN) monitor.db monitor.db-*

run-server: $(SERVER_BIN)
	./$(SERVER_BIN)

run-agent: $(AGENT_BIN)
	./$(AGENT_BIN) 127.0.0.1 5555 2
