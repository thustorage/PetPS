#include <libpmemobj++/make_persistent.hpp>
#include <libpmemobj++/p.hpp>
#include <libpmemobj++/persistent_ptr.hpp>
#include <libpmemobj++/pool.hpp>

#include <iterator>
#include <thread>
#include <vector>
#include <cstdio>
#include <unistd.h>

#include "../libpmemobj_cpp_examples_common.hpp"
#include <libpmemobj++/experimental/clevel_hash.hpp>
#include "../../../common/hash_api.h"
#define LAYOUT "clevel_hash"
// #define KEY_LEN 15
// #define VALUE_LEN 16
// #define HASH_POWER 16
namespace nvobj = pmem::obj;

typedef nvobj::experimental::clevel_hash<nvobj::p<uint64_t>, nvobj::p<uint64_t>>
	persistent_map_type;

struct root
{
	nvobj::persistent_ptr<persistent_map_type> cons;
};
uint64_t coo = 0;
uint64_t ccc = 0;
uint64_t wr = 0;
uint64_t ri = 0;
class clevel : public hash_api
{
	nvobj::pool<root> pop;
	nvobj::persistent_ptr<persistent_map_type> map;

public:

	std::string hash_name() { std::cout << "wrong: " << wr << std::endl; return "clevel"; };

	clevel(int tnum = 1)
	{
		const char *path = "/mnt/pmem";
		const size_t pool_size = 64UL * 1024 * 1024 * 1024;
		pop = nvobj::pool<root>::create(
			path, LAYOUT, pool_size, S_IWUSR | S_IRUSR);
		auto proot = pop.root();
		nvobj::transaction::manual tx(pop);
		proot->cons = nvobj::make_persistent<persistent_map_type>();
		proot->cons->set_thread_num(tnum);
		map = proot->cons;
		nvobj::transaction::commit();
	}

	clevel(const std::string & optpath, const size_t optpool_size, int tnum = 1)
	{
		if(access(optpath.c_str(), F_OK) == 0) {
			std::cout << "Reinitialize" << std::endl;
			pop = nvobj::pool<root>::open(optpath.c_str(), LAYOUT);
			auto proot = pop.root();
			map = proot->cons;
		} else {
			pop = nvobj::pool<root>::create(
			optpath.c_str(), LAYOUT, optpool_size, S_IWUSR | S_IRUSR);

			auto proot = pop.root();
			nvobj::transaction::manual tx(pop);
			proot->cons = nvobj::make_persistent<persistent_map_type>();
			proot->cons->set_thread_num(tnum);
			map = proot->cons;
			nvobj::transaction::commit();
		}
	}

	hash_Utilization utilization()
	{
		hash_Utilization h;
		h.load_factor = (float)coo / ccc;
		return h;
	}

	bool find(const char *key, size_t key_sz, char *value_out, unsigned tid)
	{
		pmem::obj::p<uint64_t> k = *reinterpret_cast<const uint64_t *>(key);
		auto r = map->search(persistent_map_type::key_type(k));
		// std::cout << r.nvmps_value_ptr << std::endl;
		memcpy(value_out, (char*)&(r.nvmps_value_ptr), sizeof(uint64_t));
		return r.found;
	}
	bool insert(const char *key, size_t key_sz, const char *value,
				size_t value_sz, unsigned tid, unsigned t)
	{
		auto k = *reinterpret_cast<const uint64_t *>(key);
		auto v = *reinterpret_cast<const uint64_t *>(value);


		auto r = map->insert(persistent_map_type::value_type(k, v), tid, t);
		
		while (!r.found)
		{
			coo++;
			ccc = r.capacity;
			r = map->insert(persistent_map_type::value_type(k, v), tid, t);
		}

	 	auto rr = map->search(persistent_map_type::key_type(k));
		if(rr.nvmps_value_ptr != v) {
			wr += 1;
		} else {
			ri += 1;
		}

		if((wr + ri) % 1000000 == 0) {
			printf("wr: %d ---- ri %d\n", (int)wr, (int)ri);
		}

		return rr.found;
	}
	bool insertResize(const char *key, size_t key_sz, const char *value,
					  size_t value_sz, unsigned tid, unsigned t)
	{
		auto k = *reinterpret_cast<const uint64_t *>(key);
		auto v = *reinterpret_cast<const uint64_t *>(value);
		auto r = map->insert(persistent_map_type::value_type(k, v), tid, t);
		return r.expanded;
	}
	bool update(const char *key, size_t key_sz, const char *value,
				size_t value_sz)
	{
		return true;
	}

	bool remove(const char *key, size_t key_sz, unsigned tid)
	{
		auto k = *reinterpret_cast<const uint64_t *>(key);
		auto r = map->erase(persistent_map_type::key_type(k), tid);
		return true;
	}

	int scan(const char *key, size_t key_sz, int scan_sz, char *&values_out)
	{
		return scan_sz;
	}
};
// extern "C" hash_api *create_hashtable(const hashtable_options_t &opt, unsigned sz = 1024, unsigned tnum = 1)
// {
// 	clevel *c = new clevel(tnum);
// 	return c;
// }

hash_api *create_hashtable_clevel(const hashtable_options_t &opt, unsigned sz = 1024, unsigned tnum = 1)
{
	clevel *c = new clevel(opt.pool_path, opt.pool_size, tnum);
	// clevel *c = new clevel(tnum);
	return c;
}