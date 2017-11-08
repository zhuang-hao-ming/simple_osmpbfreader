
#include <osmpbf/fileformat.pb.h>
#include <osmpbf/osmformat.pb.h>

#include <netinet/in.h>

#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <map>
#include <zlib.h>


using namespace std;
/*
	解析.pbf文件
	输出文件的基本信息

*/
typedef map<string, string> Tags;

class Node {
public:
	int id;
	double lon;
	double lat;
	Tags tags;
	Node(int id, double lon, double lat, Tags tags): id(id), lon(lon), lat(lat), tags(tags){}

};

class Way {
public:
	uint64_t id;
	vector<uint64_t> refs;
	Tags tags;
	Way(uint64_t id, vector<uint64_t> refs, Tags tags): id(id), refs(refs), tags(tags) {}
};

class Reference {
public:
	uint64_t id;
	string role;
	OSMPBF::Relation::MemberType member_type;

	Reference(uint64_t id, string role, OSMPBF::Relation::MemberType member_type): id(id), role(role), member_type(member_type) {}

};

class Relation {
public:
	uint64_t id;
	vector<Reference> refs;
	Tags tags;
	Relation(uint64_t id, vector<Reference> refs, Tags tags): id(id), refs(refs), tags(tags) {}
};

template<typename T>
Tags get_tags(T object, const OSMPBF::PrimitiveBlock& primitive_block) {
	Tags result;
	for (int i = 0; i < object.keys_size(); i++) {
		uint64_t key_idx = object.keys(i);
		uint64_t val_idx = object.vals(i);
		string key_string = primitive_block.stringtable().s(key_idx);
		string val_string = primitive_block.stringtable().s(val_idx);
		result[key_string] = val_string;
	}
	return result;
}


int main(int argc, char* argv[]) {

	const int MAX_BLOB_HEADER_SIZE = 64 * 1024; // 最大的BlobHeader大小
	const int MAX_BLOB_SIZE = 32 * 1024 * 1024; // 最大的Blob大小

	string pbf_name = "shenzhen_china.osm.pbf";
	ifstream file(pbf_name, ios::binary);

	char* buffer = new char[MAX_BLOB_SIZE];
	char* unpack_buffer = new char[MAX_BLOB_SIZE];


	vector<Node> node_vec;
	vector<Way> way_vec;
	vector<Relation> relation_vec;

	while (file) {

		int32_t sz;
		if (!file.read((char*)&sz, 4)) {
			cerr << "read header size fail!" << endl;
			break;
		}
		sz = ntohl(sz);

		if (sz > MAX_BLOB_HEADER_SIZE) {
			cerr << "excess MAX_BLOB_HEADER_SIZE" << endl;
			break;
		}

		if (!file.read(buffer, sz)) {
			cerr << "read blob-header fail" << endl;
			break;
		}

		OSMPBF::BlobHeader blob_header;
		if (!blob_header.ParseFromArray(buffer, sz)) {
			cerr << "unable to parse blob_header" << endl;
			break;
		}

		sz = blob_header.datasize();
		if (sz > MAX_BLOB_SIZE) {
			cerr << "excess MAX_BLOB_SIZE" << endl;
			break;
		}


		OSMPBF::Blob blob;				
		if (!file.read(buffer, sz)) {
			cerr << "read blob fail" << endl;
			break;
		}

		if (!blob.ParseFromArray(buffer, sz)) {
			cerr << "unable to parse blob " << endl;
			break;
		}
	
		if (blob.has_raw()) {
			sz = blob.raw().size();
			cout << "uncompressed size: " << sz << endl;
			
			memcpy(unpack_buffer, blob.raw().c_str(), sz);
		}
		
		if (blob.has_zlib_data()) {
			sz = blob.zlib_data().size();
			cout << "zlib compressed size: " << sz << endl;
			cout << "raw size: " << blob.raw_size() << endl;
	
			z_stream z;
			z.next_in = (unsigned char*)blob.zlib_data().c_str();
			z.avail_in = sz;
			z.next_out = (unsigned char*)unpack_buffer;
			z.avail_out = blob.raw_size();
			z.zalloc = Z_NULL;
			z.zfree = Z_NULL;
			z.opaque = Z_NULL;
	
			if (inflateInit(&z) != Z_OK) {
				cout << "fail to init zlib stream" << endl;
			}
			if (inflate(&z, Z_FINISH) != Z_STREAM_END) {
				cout << "fail to inflate zlib stream" << endl;
			}
			if (inflateEnd(&z) != Z_OK) {
				cout << "fail to deinit zlib stream" << endl;
			}
			sz = z.total_out;			
		}

		
		if (blob_header.type() == "OSMHeader") {
			cout << "header" << endl;
		} else if (blob_header.type() == "OSMData") {
			OSMPBF::PrimitiveBlock primitive_block;
			if (!primitive_block.ParseFromArray(unpack_buffer, sz)) {
				cout << "unable to parse primitive block" << endl;
				break;
			}
			for(int i = 0; i < primitive_block.primitivegroup_size(); i++) {
				OSMPBF::PrimitiveGroup pg = primitive_block.primitivegroup(i);

				for(int i = 0; i < pg.nodes_size(); i++) {
					OSMPBF::Node n = pg.nodes(i);
					double lon = 0.000000001 * (primitive_block.lon_offset() +  n.lon() * primitive_block.granularity());
					double lat = 0.000000001 * (primitive_block.lat_offset() + n.lat() * primitive_block.granularity());
					int id = n.id();
					auto tags = get_tags(n, primitive_block);
					node_vec.push_back(Node(id, lon, lat, tags));
				}

				if (pg.has_dense()) {
					OSMPBF::DenseNodes dn = pg.dense();
					uint64_t id = 0;
					double lon = 0;
					double lat = 0;
					int current_kv = 0;

					for (int i = 0; i < dn.id_size(); i++) {
						id += dn.id(i); // delta
						lon += 0.000000001 * (primitive_block.lon_offset() + dn.lon(i) * primitive_block.granularity()); // delta
						lat += 0.000000001 * (primitive_block.lat_offset() + dn.lat(i) * primitive_block.granularity());

						Tags tags;
						while ( current_kv < dn.keys_vals_size() && dn.keys_vals(current_kv) != 0) {
							uint64_t key_idx = dn.keys_vals(current_kv);
							uint64_t val_idx = dn.keys_vals(current_kv + 1);
							string key_string = primitive_block.stringtable().s(key_idx);
							string val_string = primitive_block.stringtable().s(val_idx);
							current_kv += 2;
							tags[key_string] = val_string;
						}
						++current_kv;
						node_vec.push_back(Node(id, lon, lat, tags));
					}
				}

				for (int i = 0; i < pg.ways_size(); i++) {
					OSMPBF::Way w = pg.ways(i);

					vector<uint64_t> refs;
					uint64_t ref = 0; // delta
					for (int j = 0; j < w.refs_size(); ++j) {
						ref += w.refs(j);
						refs.push_back(ref); // ???????

					} 

					uint64_t id = w.id();
					Tags tags = get_tags(w, primitive_block);
					way_vec.push_back(Way(id, refs, tags));
				}

				for (int i = 0; i < pg.relations_size(); ++i) {
					OSMPBF::Relation rel = pg.relations(i);

					uint64_t id = 0;

					vector<Reference> refs;
					for(int l = 0; l < rel.memids_size(); l++) {
						id += rel.memids(l); // delta
						refs.push_back( Reference( id, primitive_block.stringtable().s(rel.roles_sid(l)), rel.types(l) ) );
					}
					relation_vec.push_back(Relation(id, refs, get_tags(rel, primitive_block)));



				}
			}

			
			
		}
		
		
	}


	cout << "node size: "<< node_vec.size() << endl;
	cout << "way size: "<< way_vec.size() << endl;
	cout << "relation size: "<< relation_vec.size() << endl;
	



	
	



}