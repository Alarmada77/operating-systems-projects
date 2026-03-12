CC = g++
CFLAGS = -std=c++17 -Wall -Wextra -g
SRCS = smash.cpp Commands.cpp signals.cpp
OBJS = $(SRCS:.cpp=.o)
EXEC = smash

# Student IDs - fill in your IDs
ID1 = 123456789
ID2 = 987654321

.PHONY: all clean submit test

all: smash

smash: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

Commands.o: Commands.cpp Commands.h
signals.o: signals.cpp signals.h Commands.h
smash.o: smash.cpp Commands.h signals.h

clean:
	rm -f $(OBJS) $(EXEC) *.zip

submit:
	@MISSING=""; \
	for f in Commands.h Commands.cpp signals.h signals.cpp smash.cpp Makefile submitters.txt; do \
		if [ ! -f "$$f" ]; then MISSING="$$MISSING $$f"; fi; \
	done; \
	if [ -n "$$MISSING" ]; then \
		echo "✘ Missing files:$$MISSING"; exit 1; \
	else \
		echo "✔ All required files found. Creating submission archive..."; \
		zip $(ID1)_$(ID2).zip Commands.h Commands.cpp signals.h signals.cpp smash.cpp Makefile submitters.txt; \
		echo "Created: $(ID1)_$(ID2).zip"; \
	fi

test:
	@echo "Running basic tests..."
	@echo "Test 1: showpid"
	@echo "showpid" | ./smash | grep -q "smash pid is" && echo "PASS" || echo "FAIL"
	@echo "Test 2: pwd"
	@echo "pwd" | ./smash | grep -q "/" && echo "PASS" || echo "FAIL"
	@echo "Test 3: empty command"
	@echo "" | ./smash && echo "PASS" || echo "FAIL"
