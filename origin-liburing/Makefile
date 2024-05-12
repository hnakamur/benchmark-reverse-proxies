target/release/origin-liburing: main.c
	mkdir -p target/release
	cc -Wall -O2 -o $@ $< -luring

clean:
	@rm -r target

.PHONY: clean
