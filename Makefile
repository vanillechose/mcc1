CFLAGS	=	-std=gnu23 -Wall -Wextra -ggdb -Og -Werror=switch -fanalyzer -Wno-analyzer-malloc-leak
INC	=	-iquote src

srcs	=	$(wildcard src/*.c)
hdrs	=	$(wildcard src/*.h) gen/parser.h src/tokens.def
objs	=	$(srcs:.c=.o) gen/parser.o

cc1: $(objs)
	$(CC) -o $@ $^

gen:
	mkdir -p gen

gen/parser.y: SHELL=bash
gen/parser.y: | gen
gen/parser.y: src/parser.y.in src/tokens.def
	echo 'FOREACH_TOKEN(s)' \
	| $(CC) -D's(_, t)=%token t@' -include src/tokens.def -xc - -E -P -o - \
	| sed -e 's/@\s*/\n/g' \
	| cat <(sed -e '/@@TOKENS@@/,$$d' < $<) - <(sed -e '1,/@@TOKENS@@/d' $<) > $@

gen/parser.c gen/parser.h: gen/parser.y
	bison --output=gen/parser.c --header=gen/parser.h --warnings=all -Wcounterexamples $<

gen/parser.o: CFLAGS += -Wno-unused-function -fno-analyzer

src/%.o: gen/parser.h
src/%.o: INC += -iquote gen

%.o: %.c $(hdrs)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

alltests	=	$(wildcard t/*.c)

.PHONY: test
test: cc1 $(alltests)
	for t in $(alltests); do \
		if ./cc -o test < "$$t" 2> /dev/null ; then \
			if ./test; then \
				echo "=> Test $$t passed"; \
			else \
				echo "!!! Test $$t failed (execute)"; \
			fi \
		else \
			echo "!!! Test $$t failed (compile)"; \
		fi \
	done
	rm -f test

.PHONY: clean
clean:
	rm -rf $(objs) gen cc1 test
