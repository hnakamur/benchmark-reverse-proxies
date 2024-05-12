target/release/origin-liburing: main.c
	mkdir -p target/release
	cc -Wall -O2 -o $@ $< -luring

format:
	clang-format -i main.c

clean:
	@rm -r target

.PHONY: format clean
