#include <tinycode/tree.hpp>

#include <iostream>
#include <queue>

namespace TinyCode {
	namespace Tree {
		void PrintTree(Node* root, std::string str) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				std::cout << root->data << ": " << str << std::endl;
			}

			PrintTree(root->left, str + "0");
			PrintTree(root->right, str + "1");
		}

		void BuildRepresentation(Node* root, NodeRepresentation rep, std::unordered_map<uint64_t, Node*>& rep_map) {
			if(!root) {
				return;
			}

			if(root->data != 0) {
				root->representation = rep;
				rep_map[root->data]  = root;
			}

			// Further build representation
			BuildRepresentation(
				root->left, NodeRepresentation { rep.representation << 1, (uint8_t)(rep.bit_size + 1) }, rep_map);
			BuildRepresentation(root->right,
				NodeRepresentation { (rep.representation << 1) | 0b00000001, (uint8_t)(rep.bit_size + 1) }, rep_map);
		}

		Node* BuildHuffman(std::vector<Node> nodes) {
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
	}
}