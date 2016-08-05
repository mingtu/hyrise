#include "../common.hpp"
#include "table.hpp"

namespace opossum {

class table;

class record_id_list_t {
public:
    record_id_list_t(const table* table) : _table(table) {};

    void add_chunk(chunk_id_t chunk_id, chunk_row_id_list_t&& chunk_row_id_list) {
        _record_ids.emplace(chunk_id, chunk_row_id_list);
    }

    void print() DEV_ONLY {
        for (auto const& record_id : _record_ids) {
            chunk_id_t chunk_id = record_id.first;
            chunk_row_id_list_t chunk_row_id_list = record_id.second;
            std::cout << "Chunk ID: " << chunk_id << " Chunk Row IDs: ";
            for (auto const& chunk_row_id : chunk_row_id_list) {
                std::cout << chunk_row_id << " ";
            }
            std::cout << std::endl;
        }
    }

    const table* _table;
    std::map<chunk_id_t, chunk_row_id_list_t> _record_ids;
};

}