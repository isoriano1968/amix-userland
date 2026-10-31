/* stl.cpp -- does the part of the STL that OpenTTD 1.0.5 actually uses work?
 *
 * 1.0.5 avoids the STL by policy but not completely. Counted in its sources,
 * outside 3rdparty and the host-side build tools: std::map in 18 files,
 * std::list in 11, std::set in 2, std::vector in 1, std::string in 2 (the tar
 * reader), and 28 uses of dynamic_cast. There are NO iostreams anywhere, which
 * is the part of libstdc++ that would drag in locales and wide characters.
 *
 * So this is the real question for a freestanding libstdc++: the containers are
 * header-only templates, but they call out to libstdc++ for the red-black tree
 * that std::map and std::set are built on, for the __throw_* helpers, and for
 * std::string's out-of-line parts. Each step below exercises one of those. */
#include <stdio.h>
#include <map>
#include <list>
#include <set>
#include <vector>
#include <string>
#include <stdexcept>

struct Base { virtual ~Base() {} };
struct Derived : Base { int tag; Derived() : tag(99) {} };

int main()
{
	printf("1 vector:\n"); fflush(stdout);
	{
		std::vector<int> v;
		for (int i = 0; i < 5; i++) v.push_back(i * i);
		printf("   size=%d back=%d (want 5 and 16)\n", (int)v.size(), v.back());
		fflush(stdout);
	}

	printf("2 list:\n"); fflush(stdout);
	{
		std::list<int> l;
		l.push_back(2); l.push_front(1); l.push_back(3);
		int sum = 0;
		for (std::list<int>::iterator it = l.begin(); it != l.end(); ++it) sum += *it;
		printf("   size=%d sum=%d (want 3 and 6)\n", (int)l.size(), sum);
		fflush(stdout);
	}

	/* std::map and std::set need libstdc++'s red-black tree, which is NOT
	 * header-only: _Rb_tree_insert_and_rebalance and friends are compiled
	 * into the library. */
	printf("3 map (red-black tree, out of line in libstdc++):\n"); fflush(stdout);
	{
		std::map<int, int> m;
		for (int i = 0; i < 20; i++) m[i * 7 % 20] = i;
		printf("   size=%d m[3]=%d (want 20 and 9)\n", (int)m.size(), m[3]);
		fflush(stdout);
	}

	printf("4 set:\n"); fflush(stdout);
	{
		std::set<int> s;
		s.insert(5); s.insert(1); s.insert(5);
		printf("   size=%d first=%d (want 2 and 1)\n", (int)s.size(), *s.begin());
		fflush(stdout);
	}

	/* std::map<std::string, ...> is exactly what OpenTTD's tar reader uses. */
	printf("5 string, and a map keyed by one:\n"); fflush(stdout);
	{
		std::string a = "data/";
		std::string b = a + "opengfx.grf";
		std::map<std::string, int> tar;
		tar[b] = 42;
		printf("   \"%s\" len=%d  lookup=%d (want 16 and 42)\n",
		       b.c_str(), (int)b.length(), tar[b]);
		fflush(stdout);
	}

	printf("6 dynamic_cast (RTTI):\n"); fflush(stdout);
	{
		/* Both objects reached through Base* so the compiler cannot decide
		 * either cast statically -- otherwise it answers without RTTI and the
		 * probe proves nothing. */
		Derived d;
		Base bare;
		Base *hit = &d;
		Base *miss = &bare;
		Derived *q = dynamic_cast<Derived *>(hit);
		Derived *r = dynamic_cast<Derived *>(miss);
		printf("   hit=%d miss=%s (want 99 and null)\n",
		       q != 0 ? q->tag : -1, r == 0 ? "null" : "NOT NULL");
		fflush(stdout);
	}

	printf("7 out_of_range from libstdc++, caught:\n"); fflush(stdout);
	{
		std::vector<int> v;
		v.push_back(1);
		try {
			(void)v.at(5);
			printf("   FAILED -- at(5) did not throw\n");
		} catch (const std::out_of_range &) {
			printf("   caught std::out_of_range\n");
		} catch (...) {
			printf("   caught something, but not out_of_range\n");
		}
		fflush(stdout);
	}

	printf("all seven done\n");
	return 0;
}
