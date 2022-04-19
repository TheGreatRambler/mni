#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace TinyCode {
	namespace Tree {
		struct NodeRepresentation {
			uint64_t representation { 0 };
			uint8_t bit_size { 0 };
		};

		struct Node {
			uint64_t data;
			uint64_t freq;
			Node* left;
			Node* right;
			NodeRepresentation representation;

			Node(uint64_t data, uint64_t freq)
				: data { data }
				, freq { freq } {
				left = right = NULL;
			}
		};

		void PrintTree(Node* root, std::string str);
		void BuildRepresentation(Node* root, NodeRepresentation rep, std::unordered_map<uint64_t, Node*>& rep_map);
		Node* BuildHuffman(std::vector<uint64_t> data, std::vector<uint64_t> freq);
		void FreeTree(Node* root);
	}
}