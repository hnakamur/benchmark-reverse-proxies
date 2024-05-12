target/release/origin-liburing: main.c
	mkdir -p target/release
	cc -Wall -O2 -o $@ $< -luring

target/debug/origin-liburing: main.c
	mkdir -p target/debug
	cc -Wall -g -O0 -o $@ $< -luring

format:
	clang-format -i main.c

clean:
	@rm -r target

.PHONY: format clean
