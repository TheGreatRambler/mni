#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace TinyCode {
	namespace Tree {
		struct NodeRepresentation {
			int64_t representation { 0 };
			uint8_t bit_size { 0 };

			bool operator==(const NodeRepresentation& other) const {
				return representation == other.representation && bit_size == other.bit_size;
			}
		};

		struct Node {
			int64_t data;
			uint64_t freq;
			Node* left;
			Node* right;
			NodeRepresentation representation;

			Node()
				: data { 0 }
				, freq { 0 } {
				left = right = NULL;
			}

			Node(int64_t data, uint64_t freq)
				: data { data }
				, freq { freq } {
				left = right = NULL;
			}
		};

		template <typename T> void PrintTree(Node* root, std::string str);
		void BuildRepresentation(
			Node* root, NodeRepresentation rep, std::unordered_map<int64_t, NodeRepresentation>& rep_map);
		void BuildRepresentation(Node* root, std::unordered_map<int64_t, NodeRepresentation>& rep_map);
		Node* BuildHuffman(std::vector<Node>& nodes);
		void FreeTree(Node* root);
	}
}

namespace std {
	template <> struct hash<TinyCode::Tree::NodeRepresentation> {
		std::size_t operator()(const TinyCode::Tree::NodeRepresentation& k) const {
			return ((std::hash<uint64_t>()(k.representation) ^ (std::hash<uint8_t>()(k.bit_size) << 1)) >> 1);
		}
	};
}