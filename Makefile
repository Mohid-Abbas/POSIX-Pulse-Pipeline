CC = gcc
CFLAGS = -Wall -Wextra -pthread -Isrc -D_GNU_SOURCE
LDFLAGS = -lrt -pthread

TARGETS = dispatcher ingester processor reporter

all: $(TARGETS)

dispatcher: src/dispatcher.c src/common/common.h
	$(CC) $(CFLAGS) src/dispatcher.c -o dispatcher $(LDFLAGS)

ingester: src/ingester.c src/common/common.h
	$(CC) $(CFLAGS) src/ingester.c -o ingester $(LDFLAGS)

processor: src/processor.c src/common/common.h
	$(CC) $(CFLAGS) src/processor.c -o processor $(LDFLAGS)

reporter: src/reporter.c src/common/common.h
	$(CC) $(CFLAGS) src/reporter.c -o reporter $(LDFLAGS)

clean:
	rm -f $(TARGETS) logs/*.log

.PHONY: all clean
