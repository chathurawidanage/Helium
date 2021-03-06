#include <ostream>
#include <fstream>
#include <algorithm>

using namespace std;

#include "trees/trees.h"
#include "analysis/x86_analysis.h"
#include "common_defines.h"
#include "utility/defines.h"
#include "utility/print_helper.h"

#include "utilities.h"


Tree::Tree(){

	head = NULL;
	num_nodes = 0;
	tree_num = -1;
	recursive = false;

}

void Tree::set_head(Node * node){
	head = node;
}

void Tree::collect_all_nodes(Node * node, std::vector<Node *> &nodes)
{
	if (find(nodes.begin(), nodes.end(), node) == nodes.end()){
		nodes.push_back(node);
	}

	for (int i = 0; i < node->srcs.size(); i++){
		collect_all_nodes(node->srcs[i], nodes);
	}
}

Tree::~Tree(){

	vector<Node *> nodes;
	collect_all_nodes(head, nodes);
	for (int i = 0; i < nodes.size(); i++){
		delete nodes[i];
	}


}

Node * Tree::get_head()
{
	return head;
}


/* pre-order traversal of the tree */
void * Tree::traverse_tree(Node * node, void * value, node_mutator mutator, return_mutator ret_mutator){

	void * node_val = mutator(node, value);
	vector<void *> traverse_val;


	for (int i = 0; i < node->srcs.size(); i++){
		traverse_val.push_back(traverse_tree(node->srcs[i], value, mutator, ret_mutator));
	}

	return ret_mutator(node_val, traverse_val, value); 
} 

void Tree::canonicalize_tree()
{
	uint32_t changed_g = 1;
	/* first congregate the tree and then order the nodes */
	while (changed_g){
		changed_g = (uint32_t)traverse_tree(head, this,
			[](Node * node, void * value)->void* {

			Tree * tree = static_cast<Tree *>(value);
			uint32_t changed = node->congregate_node(tree->get_head());
			return (void *)changed;

		}, [](void * node_value, std::vector<void *> traverse_value, void * value)->void*{

			uint32_t changed = (uint32_t)node_value;
			if (changed) return (void *)changed;

			for (int i = 0; i < traverse_value.size(); i++){
				changed = (uint32_t)traverse_value[i];
				if (changed) return (void *)changed;
			}

			return (void *)changed;

		});
	}

	traverse_tree(head, NULL,
		[](Node * node, void * value)->void* {

		node->order_node();
		return NULL;
	}, empty_ret_mutator);


}

void Tree::simplify_tree() /* simplification routines are not written at the moment */
{

}


void Tree::print_dot(std::ostream &file, string name, uint32_t number)
{

	DEBUG_PRINT(("printing tree to dot file\n"), 2);

	/* print the nodes */
	string nodes = "";

	file << "digraph G_" << name << "_" << number << " {" << endl;

	cleanup_visit();

	traverse_tree(head, &nodes,
		[](Node * node, void * value)->void* {


		if (node->visited == false){
			string * node_string = static_cast<string *>(value);
			*node_string += dot_get_node_string(node->order_num, node->get_dot_string()) + "\n";
			node->visited = true;
		}

		return NULL;

	}, empty_ret_mutator);

	file << nodes << endl;

	cleanup_visit();

	/* print the edges */
	string edges = "";

	traverse_tree(head, &edges,
		[](Node * node, void * value)->void* {


		if (node->visited == false){
			string * edge_string = static_cast<string *>(value);
			for (int i = 0; i < node->srcs.size(); i++){
				*edge_string += dot_get_edge_string(node->order_num, node->srcs[i]->order_num) + "\n";
			}
			node->visited = true;
			
		}

		return NULL;

	}, empty_ret_mutator);

	cleanup_visit();

	file << edges << endl;

	file << "}" << endl;

}

void Tree::print_tree_recursive(Node * node, std::ostream &file){

	int no_srcs = node->srcs.size();

	if (no_srcs == 0){ /* we are at a leaf just print it*/
		file << opnd_to_string(node->symbol);
	}
	else if (no_srcs == 1){ /* unary operation */

		/* we can have a full overlap - partial overlaps will have more than one operand */
		if (node->operation == op_full_overlap){
			file << "{" << opnd_to_string(node->symbol) << " -> " << opnd_to_string(node->srcs[0]->symbol) << "}";
			file << "(";
			print_tree_recursive(node->srcs[0], file);
			file << ")";
		}
		else if (node->operation == op_assign){
			print_tree_recursive(node->srcs[0], file);
		}
		else{
			file << operation_to_string(node->operation) << " ";
			print_tree_recursive(node->srcs[0], file);
		}

	}
	else if ((no_srcs == 2) && (node->operation != op_partial_overlap)){
		file << "(";
		print_tree_recursive(node->srcs[0], file);
		file << " " << operation_to_string(node->operation) << " ";
		print_tree_recursive(node->srcs[1], file);
		file << ")";
	}
	else{
		//ASSERT_MSG((node->operation == op_partial_overlap), ("ERROR: unexpected operation with more than two srcs\n"));
		/*here it is important to see how each source contributes
		another important problem is that what source updates first
		*/
		/*file << "(";
		for (int i = 0; i < no_srcs; i++){
		print_tree(node->srcs[0], file);
		if (i != no_srcs - 1){
		file << "," << endl;
		}
		}
		file << ")";*/
		file << "(";
		file << operation_to_string(node->operation) << " ";
		for (int i = 0; i < no_srcs; i++){
			print_tree_recursive(node->srcs[0], file);
			if (i != no_srcs - 1){
				file << ",";
			}
		}
		file << ")";

	}

}

void Tree::print_tree(std::ostream &file)
{
	print_tree_recursive(head, file);

}

void * node_numbering(Node * node, void * value){
	if (node->order_num == -1){
		node->order_num = (*(int *)value)++;
	}
	return NULL;
}

void * empty_ret_mutator(void * value, vector<void *> values, void * ori_value){
	return NULL;
}

void Tree::number_tree_nodes()
{
	DEBUG_PRINT(("numbering tree\n"), 4);
	traverse_tree(head, &num_nodes, node_numbering, empty_ret_mutator);
	DEBUG_PRINT(("number of nodes %d\n", num_nodes), 4);
}

void Tree::cleanup_visit()
{
	traverse_tree(head, &num_nodes,
		[](Node * node, void * value)->void* {
		node->visited = false;
		return NULL;
		}, empty_ret_mutator);
}

void Tree::copy_exact_tree_structure(Tree * tree, void * peripheral_data, node_to_node node_creation)
{
	/* assumption the tree is numbered and the visited state is cleared */
	ASSERT_MSG((tree->get_head()->order_num != -1), ("ERROR: the concrete tree is not numbered\n"));
	ASSERT_MSG((tree->get_head()->visited == false), ("ERROR: the visit state of the concrete tree is not cleared\n"));


	/* get all the nodes and the nodes to which it is connected */
	vector< pair<Node *, vector<int> > > tree_map;
	tree_map.resize(tree->num_nodes); /* allocate space for the the nodes */
	for (int i = 0; i < tree_map.size(); i++){
		tree_map[i].first = NULL;
	}


	traverse_tree(tree->get_head(), &tree_map,
		[](Node * node, void * value)->void* {

		vector< pair<Node *, vector<int> > > * map = (vector< pair<Node *, vector<int> > > *)value;
		if ((*map)[node->order_num].first == NULL){
			for (int i = 0; i < node->srcs.size(); i++){
				(*map)[node->order_num].second.push_back(node->srcs[i]->order_num);
			}
		}
		return NULL;

	}, empty_ret_mutator);

	/* now create the new tree as a vector */
	vector < pair<Node *, vector<int> > > new_tree_map;

	for (int i = 0; i < new_tree_map.size(); i++){

		new_tree_map.push_back(make_pair(
			node_creation(tree->get_head(),tree_map[i].first, peripheral_data),
			tree_map[i].second)
			);
	}


	set_head(new_tree_map[0].first);
	/* now create the linkage structure */
	for (int i = 0; i < new_tree_map.size(); i++){
		Node * node = new_tree_map[i].first;
		vector<int> srcs = new_tree_map[i].second;
		for (int j = 0; j < srcs.size(); i++){
			node->srcs.push_back(new_tree_map[srcs[j]].first);
			new_tree_map[srcs[j]].first->prev.push_back(node);
			new_tree_map[srcs[j]].first->pos.push_back(j);
		}
	}

	/* done! -> may have a better algorithm check */


}

void Tree::copy_unrolled_tree_structure(Tree * tree, void * peripheral_data, node_to_node node_creation){

	set_head(node_creation(tree->get_head(),tree->get_head(),peripheral_data));
	copy_unrolled_tree_structure(tree->get_head(),tree->get_head(), get_head(), peripheral_data, node_creation);

}

void Tree::copy_unrolled_tree_structure(Node * head, Node * from, Node * to, void * peripheral_data, node_to_node node_creation){

	for (int i = 0; i < from->srcs.size(); i++){
		Node * new_node = node_creation(head, from->srcs[i], peripheral_data);
		to->srcs.push_back(new_node);
		new_node->prev.push_back(to);
		new_node->pos.push_back(i);
		copy_unrolled_tree_structure(head,from->srcs[i], new_node, peripheral_data, node_creation);
	}

}

void Tree::change_head_node()
{
	if (head->operation == op_assign){ /* then this node can be removed */
		ASSERT_MSG((head->srcs.size() == 1), ("ERROR: unexpected number of operands\n"));
		Node * new_head = head->srcs[0];
		new_head->prev.clear();
		new_head->pos.clear();
		delete head;
		head = new_head;
	}
}


bool Tree::are_trees_similar(Tree * tree)
{
	vector<Node *> nodes;
	nodes.push_back(get_head());
	nodes.push_back(tree->get_head());
	return are_trees_similar(nodes);
}

bool Tree::are_trees_similar(std::vector<Tree *> trees)
{

	vector<Node *> nodes;
	for (int i = 0; i < trees.size(); i++){
		nodes.push_back(trees[i]->get_head());
	}

	return are_trees_similar(nodes);

}

bool Tree::are_trees_similar(std::vector<Node *> node)
{
	if (!Node::are_nodes_similar(node)) return false;
	

	if (node.size() > 0){
		for (int i = 0; i < node[0]->srcs.size(); i++){
			vector<Node *> nodes;
			for (int j = 0; j < node.size(); j++){
				nodes.push_back(node[j]->srcs[i]);
			}
			if (!are_trees_similar(nodes)) return false;
		}
	}
	
	return true;

}


void Tree::remove_assign_nodes()
{


	cleanup_visit();

	traverse_tree(head, head, [](Node * dst, void * value)->void*{


		Node * head = (Node *)value;

		if (dst->operation == op_assign && dst != head && dst->srcs.size() == 1){
			

			ASSERT_MSG((dst->srcs.size() == 1), ("ERROR: assign should only have one source\n"));
			Node * src = dst->srcs[0]; 

			for (int i = 0; i < dst->prev.size(); i++){

				src->prev.push_back(dst->prev[i]);
				src->pos.push_back(dst->pos[i]);
				dst->prev[i]->srcs[dst->pos[i]] = src;

			}

		}

		return NULL;

	}, empty_ret_mutator);
}

std::vector<mem_regions_t *> Tree::identify_intermediate_buffers(std::vector<mem_regions_t *> mem)
{


	vector<mem_regions_t *> regions;



	return regions;

}


bool Tree::is_recursive(Node * node, vector<mem_regions_t *> &regions){

	if (head != node){

		if (node->symbol->type == MEM_HEAP_TYPE || node->symbol->type == MEM_STACK_TYPE){
			if (get_mem_region(head->symbol->value, regions) == get_mem_region(node->symbol->value, regions)){
				return true;
			}
		}
		
	}

	bool ret = false;
	for (int i = 0; i < node->srcs.size(); i++){
		ret = is_recursive(node->srcs[i], regions);
		if (ret){ break; }
	}

	return ret;

}


bool remove_multiplication_internal(Node * dst){

	if (dst->visited) return false;
	else{

		bool mul = false;
		dst->visited = true;

		if (dst->operation == op_mul){

			int index = -1;
			int imm_value = 0;

			for (int i = 0; i < dst->srcs.size(); i++){
				if (dst->srcs[i]->symbol->type == IMM_INT_TYPE){
					imm_value = dst->srcs[i]->symbol->value; index = i;
					break;
				}
			}

			if (index != -1 && imm_value < 10 && imm_value >= 0){
				mul = true;
				for (int i = 0; i < dst->prev.size(); i++){
					for (int j = 0; j < dst->srcs.size(); j++){
						if (index != j){
							for (int k = 0; k < imm_value; k++){
								dst->prev[i]->add_forward_ref(dst->srcs[j]);
							}
						}
					}
					dst->prev[i]->remove_forward_ref_single(dst);
				}

				for (int i = 0; i < dst->srcs.size(); i++){
					dst->remove_forward_ref(dst->srcs[i]);
				}
			}
		}

		for (int i = 0; i < dst->srcs.size(); i++){
			if (remove_multiplication_internal(dst->srcs[i])) i--;
		}


		return mul;

	}
	


}


void Tree::remove_multiplication(){

	cleanup_visit();

	/*traverse_tree(head, head, [](Node * dst, void * value)->void*{

		if (!dst->visited){

			dst->visited = true;

			if (dst->operation == op_mul){

				int index = -1;
				int imm_value = 0;

				for (int i = 0; i < dst->srcs.size(); i++){
					if (dst->srcs[i]->symbol->type == IMM_INT_TYPE){
						imm_value = dst->srcs[i]->symbol->value; index = i;
						break;
					}
				}

				if (index != -1 && imm_value < 10 && imm_value >= 0){
					for (int i = 0; i < dst->prev.size(); i++){
						for (int j = 0; j < dst->srcs.size(); j++){
							if (index != j){
								for (int k = 0; k < imm_value; k++){
									dst->prev[i]->add_forward_ref(dst->srcs[j]);
								}
							}
						}
						dst->prev[i]->remove_forward_ref(dst);
					}

					for (int i = 0; i < dst->srcs.size(); i++){
						dst->remove_forward_ref(dst->srcs[i]);
					}
				}
			}
		}
		return NULL;

	}, empty_ret_mutator);*/

	remove_multiplication_internal(head);

	cleanup_visit();

}

/* make sure all nodes are canonicalized before calling this routine */
void Tree::simplify_immediates(){

	traverse_tree(head, head, [](Node * node, void * value)->void *{

		if (node->operation == op_mul || node->operation == op_add){

			vector<uint32_t> indexes;
			int32_t value = 0 ;

			for (int i = 0; i < node->srcs.size(); i++){
				if (node->srcs[i]->symbol->type == IMM_INT_TYPE){
					value += node->srcs[i]->symbol->value;
					indexes.push_back(i);
				}
			}

			if (indexes.size() > 0){
				
				ASSERT_MSG(indexes[0] == 0, ("ERROR: canonicalized tree has immediates to the left\n"));
				node->srcs[0]->symbol->value = value;
				for (int i = 1; i < indexes.size(); i++){
					node->remove_forward_ref_single(node->srcs[indexes[i] - (i - 1)]);
				}
			}
		}


		return NULL;
	}, empty_ret_mutator);

}

bool remove_redundant_internal(Node * node){

	bool changed = false;
	if (node->operation == op_add){
		if (node->srcs.size() == 1){
			for (int i = 0; i < node->prev.size(); i++){
				node->change_ref(node->prev[i], node->srcs[0]);
				changed = true;
			}
		}
	}

	else if (node->operation == op_full_overlap){
		for (int i = 0; i < node->srcs.size(); i++){
			if (node->srcs[i]->symbol->type == IMM_INT_TYPE){
				for (int j = 0; j < node->prev.size(); j++){
					node->change_ref(node->prev[j], node->srcs[i]);
					changed = true;
				}
			}
		}
	}

	else if (node->symbol->type == IMM_INT_TYPE && node->symbol->value == 0){
		int count = 0;
		for (int i = 0; i < node->prev.size(); i++){
			if (node->prev[i]->operation == op_add){
				node->prev[i]->remove_forward_ref_single(node);
				changed = true;
				count++;
			}
		}
	}

	for (int i = 0; i < node->srcs.size(); i++){
		if (remove_redundant_internal(node->srcs[i])) i = (i - 1 >= 0) ? i-1 : 0;
	}

	return changed;

}

void Tree::remove_redundant_nodes(){

	remove_redundant_internal(head);

}

bool convert_node_sub_internal(Node * node, Node * head){

	bool changed = false;
	if (node->operation == op_sub){

		for (int i = 0; i < node->prev.size(); i++){
			if (node->prev[i]->operation == op_add){
				ASSERT_MSG(node->srcs.size() == 2, ("ERROR: sub\n"));
				
				node->prev[i]->add_forward_ref(node->srcs[0]);
				node->srcs[1]->minus = true;
				node->prev[i]->add_forward_ref(node->srcs[1]);
				changed = true;

				node->remove_forward_ref_single(node->srcs[0]);
				node->remove_forward_ref_single(node->srcs[0]);
				node->prev[i]->remove_forward_ref_single(node);
			}
			
			
		}
	}

	for (int i = 0; i < node->srcs.size(); i++){
		if (convert_node_sub_internal(node->srcs[i], head)) i--;
	}
	return changed;

}

void internal_propogate_minus(Node * node){

	if (node->minus && node->operation == op_add){
		for (int i = 0; i < node->srcs.size(); i++){
			node->srcs[i]->minus = !node->srcs[i]->minus;
		}
	}

	for (int i = 0; i < node->srcs.size(); i++){
		internal_propogate_minus(node->srcs[i]);
	}



}

void Tree::verify_minus(){

	traverse_tree(head, head, [](Node * node, void * value)->void *{

		if (node->operation == op_sub){
			ASSERT_MSG(node->srcs.size() == 2, ("ERROR: minus with <2 nodes\n"));
		}
		return NULL;
	}, empty_ret_mutator);

	DEBUG_PRINT(("sub verified\n"), 2);

}

void Tree::simplify_minus(){

	traverse_tree(head, head, [](Node * node, void * value)->void *{

		if (node->operation == op_add){
			vector<Node *> positive;
			vector<Node *> negative;

			for (int i = 0; i < node->srcs.size(); i++){
				if (node->srcs[i]->minus) negative.push_back(node->srcs[i]);
				else positive.push_back(node->srcs[i]);
			}

			//cout << "minus " << positive.size() << " " << negative.size() << endl;

			for (int i = 0; i < positive.size(); i++){
				for (int j = 0; j < negative.size(); j++){
					if (positive[i]->symbol->type == negative[j]->symbol->type &&
						positive[i]->symbol->width == negative[j]->symbol->width &&
						positive[i]->symbol->value == negative[j]->symbol->value){
						node->remove_forward_ref_single(positive[i]);
						node->remove_forward_ref_single(negative[j]);
						negative.erase(negative.begin() + j);
						break;
					}
				}
			}
		}


		return NULL;
		



	}, empty_ret_mutator);


}

void Tree::convert_sub_nodes(){

	convert_node_sub_internal(head, head);
	
	internal_propogate_minus(head);

}


/* unsound transformations */
void Tree::remove_po_nodes(){


	traverse_tree(head, head, [](Node * node, void * value)-> void *{

		if (node->operation == op_partial_overlap){
			for (int i = 0; i < node->prev.size(); i++){
				if (node->prev[i]->symbol->width == node->symbol->width){
					for (int j = 0; j < node->srcs.size(); j++){
						node->change_ref(node->prev[i],node->srcs[j]);
					}
				}
			}
		}

		return NULL;
		


	}, empty_ret_mutator);





}


void Tree::remove_or_minus_1(){

	traverse_tree(head, head, [](Node * node, void * value)-> void* {

		if (node->operation == op_or){


			int32_t index = -1;
			for (int i = 0; i < node->srcs.size(); i++){

				if (node->srcs[i]->symbol->type == IMM_INT_TYPE && (int32_t)node->srcs[i]->symbol->value == -1){
					index = i; break;
				}
			}


			if (index != -1){
				node->srcs.clear();
				node->symbol->type = IMM_INT_TYPE;
				node->symbol->value = 255;
			}

			return NULL;

		}

		return NULL;


	}, empty_ret_mutator);


}


void Tree::mark_recursive(){

	traverse_tree(head, this, [](Node * node, void * value)-> void * {


		Tree * tree = (Tree *)value;
		Node * head = tree->get_head();


		mem_regions_t * head_region = ((Conc_Node *)head)->region;
		mem_regions_t * conc_region = ((Conc_Node *)node)->region;

		if (head_region == NULL || conc_region == NULL) return NULL;

		if (head_region == conc_region && head->symbol->value != node->symbol->value){
			tree->recursive = true;
		}

		return NULL;

	}, empty_ret_mutator);



}

void Tree::remove_identities(){

	traverse_tree(head, NULL, [](Node * node, void * value)->void*{

		if (node->operation == op_add){

			for (int i = 0; i < node->srcs.size(); i++){
				if (node->srcs[i]->symbol->type == IMM_INT_TYPE && node->srcs[i]->symbol->value == 0){
					node->remove_forward_ref_single(node->srcs[i]);
					i--;
				}
			}


		}

		return NULL;


	}, empty_ret_mutator);


}




