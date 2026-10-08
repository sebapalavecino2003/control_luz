CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -g

TESTS = test_regla

.PHONY: test clean
test: $(TESTS)
	@for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || exit 1; \
	done

test_regla: test_regla.cpp ../regla_luz.h
	$(CXX) $(CXXFLAGS) -o $@ $<

clean:
	rm -f $(TESTS)
