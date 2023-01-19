#pragma once

#include <cstdint>
#include <iostream>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace Mni {
	namespace Tree {
		struct NodeRepresentation {
			uint64_t representation { 0 };
			uint8_t bit_size { 0 };

			bool operator==(const NodeRepresentation& other) const {
				return representation == other.representation && bit_size == other.bit_size;
			}
		};

		template <typename T> struct Node {
			T data;
			uint64_t freq;
			Node<T>* left;
			Node<T>* right;
			NodeRepresentation representation;

			Node()
				: data { 0 }
				, freq { 0 } {
				left = right = NULL;
			}

			Node(T data, uint64_t freq)
				: data { data }
				, freq { freq } {
				left = right = NULL;
			}
		};

		template <typename T, typename P> void PrintTree(Node<T>* root, std::string str) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				std::cout << (P)root->data << ": " << str << std::endl;
			}

			PrintTree<T, P>(root->left, str + "0");
			PrintTree<T, P>(root->right, str + "1");
		}

		template <typename T>
		void GenerateHuffman(
			std::vector<T> data, std::unordered_map<T, NodeRepresentation>& rep_map) {
			std::unordered_map<T, Node<T>> element_frequencies;
			for(T num : data) {
				if(element_frequencies.count(num)) {
					element_frequencies[num].freq++;
				} else {
					element_frequencies[num] = Node(num, 1);
				}
			}

			GenerateHuffmanFrequencies(element_frequencies, rep_map);
		}

		template <typename T>
		void GenerateHuffmanFrequencies(std::unordered_map<T, Node<T>>& frequencies,
			std::unordered_map<T, NodeRepresentation>& rep_map) {
			std::vector<Node<T>> element_frequencies_list;

			for(auto& element : frequencies) {
				element_frequencies_list.push_back(element.second);
			}

			Node<T>* root = BuildHuffman(element_frequencies_list);
			BuildRepresentation(root, rep_map);
			FreeTree(root);
		}

		template <typename T>
		void BuildRepresentation(Node<T>* root, NodeRepresentation rep,
			std::unordered_map<T, NodeRepresentation>& rep_map) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				// std::string rep_string = std::bitset<64>(rep.representation).to_string();
				// std::cout << (char)root->data << ": " << rep_string.substr(rep_string.size() -
				// rep.bit_size)
				//		  << std::endl;
				// root->representation.representation = rep.representation;
				// root->representation.bit_size       = rep.bit_size;
				rep_map[root->data] = rep;
			}

			// Further build representation
			BuildRepresentation(root->left,
				NodeRepresentation { rep.representation << 1, (uint8_t)(rep.bit_size + 1) },
				rep_map);
			BuildRepresentation(root->right,
				NodeRepresentation { (rep.representation << 1) | 0x1, (uint8_t)(rep.bit_size + 1) },
				rep_map);
		}

		template <typename T>
		void BuildRepresentation(
			Node<T>* root, std::unordered_map<T, NodeRepresentation>& rep_map) {
			BuildRepresentation(root, NodeRepresentation { 0, 0 }, rep_map);
		}

		template <typename T> Node<T>* BuildHuffman(std::vector<Node<T>>& nodes) {
			auto node_compare
				= [](Node<T>* left, Node<T>* right) { return left->freq > right->freq; };
			std::priority_queue<Node<T>*, std::vector<Node<T>*>, decltype(node_compare)> min_heap(
				node_compare);

			for(const Node<T>& node : nodes) {
				min_heap.push(new Node(node.data, node.freq));
			}

			while(min_heap.size() != 1) {
				Node<T>* left = min_heap.top();
				min_heap.pop();

				Node<T>* right = min_heap.top();
				min_heap.pop();

				Node<T>* top = new Node<T>(0, left->freq + right->freq);

				top->left  = left;
				top->right = right;

				min_heap.push(top);
			}

			return min_heap.top();
		}

		template <typename T> void FreeTree(Node<T>* root) {
			if(!root) {
				return;
			}

			FreeTree(root->left);
			FreeTree(root->right);

			free(root);
		}
	}
}

namespace std {
	template <> struct hash<Mni::Tree::NodeRepresentation> {
		std::size_t operator()(const Mni::Tree::NodeRepresentation& k) const {
			return (
				(std::hash<uint64_t>()(k.representation) ^ (std::hash<uint8_t>()(k.bit_size) << 1))
				>> 1);
		}
	};
}