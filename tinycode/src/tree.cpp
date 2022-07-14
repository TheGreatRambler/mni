#include <tinycode/tree.hpp>

#include <bitset>
#include <iostream>
#include <queue>

namespace TinyCode {
	namespace Tree {
		template <typename T> void PrintTree(Node* root, std::string str) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				std::cout << (T)root->data << ": " << str << std::endl;
			}

			PrintTree<T>(root->left, str + "0");
			PrintTree<T>(root->right, str + "1");
		}

		void GenerateHuffman(std::vector<int64_t> data, std::unordered_map<int64_t, NodeRepresentation>& rep_map) {
			std::unordered_map<int64_t, Node> element_frequencies;
			for(int64_t num : data) {
				if(element_frequencies.count(num)) {
					element_frequencies[num].freq++;
				} else {
					element_frequencies[num] = Node(num, 1);
				}
			}

			GenerateHuffmanFrequencies(element_frequencies, rep_map);
		}

		void GenerateHuffmanFrequencies(
			std::unordered_map<int64_t, Node>& frequencies, std::unordered_map<int64_t, NodeRepresentation>& rep_map) {
			std::vector<Node> element_frequencies_list;

			for(auto& element : frequencies) {
				element_frequencies_list.push_back(element.second);
			}

			Node* root = BuildHuffman(element_frequencies_list);
			BuildRepresentation(root, rep_map);
			FreeTree(root);
		}

		void BuildRepresentation(
			Node* root, NodeRepresentation rep, std::unordered_map<int64_t, NodeRepresentation>& rep_map) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				// std::string rep_string = std::bitset<64>(rep.representation).to_string();
				// std::cout << (char)root->data << ": " << rep_string.substr(rep_string.size() - rep.bit_size)
				//		  << std::endl;
				// root->representation.representation = rep.representation;
				// root->representation.bit_size       = rep.bit_size;
				rep_map[root->data] = rep;
			}

			// Further build representation
			BuildRepresentation(
				root->left, NodeRepresentation { rep.representation << 1, (uint8_t)(rep.bit_size + 1) }, rep_map);
			BuildRepresentation(root->right,
				NodeRepresentation { (rep.representation << 1) | 0x1, (uint8_t)(rep.bit_size + 1) }, rep_map);
		}

		void BuildRepresentation(Node* root, std::unordered_map<int64_t, NodeRepresentation>& rep_map) {
			BuildRepresentation(root, NodeRepresentation { 0, 0 }, rep_map);
		}

		Node* BuildHuffman(std::vector<Node>& nodes) {
			auto node_compare = [](Node* left, Node* right) { return left->freq > right->freq; };
			std::priority_queue<Node*, std::vector<Node*>, decltype(node_compare)> min_heap;

			for(const Node& node : nodes) {
				min_heap.push(new Node(node.data, node.freq));
			}

			while(min_heap.size() != 1) {
				// Extract the two minimum
				// freq items from min heap
				Node* left = min_heap.top();
				min_heap.pop();

				Node* right = min_heap.top();
				min_heap.pop();

				// Create a new internal node with
				// frequency equal to the sum of the
				// two nodes frequencies. Make the
				// two extracted node as left and right children
				// of this new node. Data is zero as it is internal
				Node* top = new Node(0, left->freq + right->freq);

				top->left  = left;
				top->right = right;

				min_heap.push(top);
			}

			return min_heap.top();
		}

		void FreeTree(Node* root) {
			if(!root) {
				return;
			}

			FreeTree(root->left);
			FreeTree(root->right);

			free(root);
		}

		template void PrintTree<int>(Node* root, std::string str);
		template void PrintTree<char>(Node* root, std::string str);
	}
}