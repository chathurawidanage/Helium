 #ifndef _TREES_H
 #define _TREES_H
 
 #include <stdint.h>
 #include "trees\nodes.h"
 #include "analysis\x86_analysis.h"
 
 /* trees and their routines */

typedef void *  (*node_mutator) (Node * node, void * value);
typedef void *  (*return_mutator) (void * node_value, std::vector<void *> traverse_value, void * value);
typedef Node *  (*node_to_node)(void * head, void * node, void * peripheral_data);

void * empty_ret_mutator(void * value, vector<void *> values, void * ori_value);

 class Tree{

 protected:
	 Node * head;
	
	 /* internal simplification routines */
	 /* fill as they are written */

 private:

	 void print_tree_recursive(Node * node, std::ostream &file);
	 void collect_all_nodes(Node * node, std::vector<Node *> &nodes);
	 static void copy_unrolled_tree_structure(Node * head,Node * from, Node * to, void * peripheral_data, node_to_node node_creation);
	 static bool are_trees_similar(std::vector<Node *> node);
 protected:
	 void * traverse_tree(Node * node, void * value, node_mutator mutator, return_mutator ret_mutator);
	 void copy_exact_tree_structure(Tree * tree, void * peripheral_data, node_to_node node_creation);
	 void copy_unrolled_tree_structure(Tree * tree, void * peripheral_data, node_to_node node_creation);


 public:

	 uint32_t num_nodes;
	 int32_t tree_num; /* this is for numbering the tree (most porabably based on output location - used in tree clustering) */
	 static uint32_t num_paras;
	 bool recursive;
	 bool dummy_tree;


	 Tree();
	 ~Tree();
	 
	 Node * get_head();
	 void set_head(Node * node);


	 /* for tree simplification -> each type of tree will have its own serialization routines */
	 virtual std::string serialize_tree() = 0;
	 virtual void construct_tree(std::string stree) = 0;

	 /* tree manipulation routines */
	 void canonicalize_tree();
	 void simplify_tree();
	 void change_head_node();

	 /* for printing purposes */
	 void number_tree_nodes();
	 void print_tree(std::ostream &file);
	 void print_dot(std::ostream &file, std::string name, uint32_t number);

	 bool are_trees_similar(Tree * tree);
	 static bool are_trees_similar(std::vector<Tree *> trees);
	 bool is_recursive(Node * node, std::vector<mem_regions_t *> &regions);
	 
	 void cleanup_visit();


	 /* some tree transformations */
	 void remove_assign_nodes();
	 std::vector<mem_regions_t *> identify_intermediate_buffers(std::vector<mem_regions_t *> mem);
	 void remove_multiplication();
	 void simplify_immediates();
	 void remove_minus_nodes();
	 void remove_redundant_nodes();
	 void convert_sub_nodes();
	 void simplify_minus();
	 void verify_minus();
	 void remove_po_nodes();
	 void remove_or_minus_1();
	 void mark_recursive();
	 void remove_identities();
 };




 /* Conc_Tree - expression tree from the instruction trace */
 
 class Conc_Tree : public Tree {

 private:

	 /* data structure for keeping the hash table bins */
	 struct frontier_t {   
		 Node ** bucket;
		 int amount;
	 };

	 frontier_t * frontier;  /*this is actually a hash table keeping pointers to the Nodes already allocated */
	 std::vector<uint32_t> mem_in_frontier; /* memoization structures for partial mem and reg writes and reads */
	 
	 bool func_inside;
	 uint32_t func_index;

	 
	 Node * create_or_get_node(operand_t * opnd);
	 void remove_from_frontier(operand_t * opnd);


	 void get_full_overlap_nodes(std::vector<Node *> &nodes, operand_t * opnd);
	 void split_partial_overlaps(std::vector < std::pair <Node *, std::vector<Node *> > > &nodes, operand_t * opnd, uint32_t hash);
	 void get_partial_overlap_nodes(std::vector< std::pair<Node *, std::vector<Node *> > > &nodes, operand_t * opnd);

	 void number_parameters(Node * node, vector<mem_regions_t *> regions);


 public:
	
	 /* this governs which conditionals affect the tree -> needed for predicate tree generation */
	struct conditional_t {

		 Jump_Info * jump_info;
		 uint32_t line_cond; // this is the cond_pc location 
		 uint32_t line_jump;
		 Conc_Tree * tree; // we need to have two expressions for a single conditional
		 bool taken;

	 };

	 std::vector<conditional_t *> conditionals;

	 Conc_Tree();
	 ~Conc_Tree();


	 Node * search_node(operand_t * opnd);
	 int generate_hash(operand_t * opnd);
	 void add_to_frontier(int hash, Node * node);
	 void remove_registers_from_frontier();

	 //bool update_depandancy_backward(rinstr_t * instr, cinstr_t * cinstr, Static_Info * info, uint32_t line);
	 bool update_depandancy_backward(rinstr_t * instr, cinstr_t * cinstr, Static_Info * info, uint32_t line, vector<mem_regions_t *> region, vector<Func_Info_t *> func_info);
	 bool update_dependancy_forward(rinstr_t * instr, uint32_t pc, std::string disasm, uint32_t line);
	 bool update_dependancy_forward_with_indirection(rinstr_t * instr, uint32_t pc, std::string disasm, uint32_t line);
	 bool update_depandancy_forward_with_src(rinstr_t * instr, uint32_t pc, std::string disasm, uint32_t line, bool * src_dep);

	 void add_address_dependancy(Node * node, operand_t * opnds);
	 void process_forward_destination(operand_t * opnd);
	 void remove_dest_forward(operand_t * opnd);

	 void number_parameters(std::vector<mem_regions_t *> regions);
	 std::string serialize_tree();
	 void construct_tree(std::string stree);
	 void print_conditionals();
	 bool tree_add_to_frontier(rinstr_t * instr, Node * src);
 };



 /* Abs tree */
 class Abs_Tree : public Tree {
 
 public:

	 Abs_Tree();
	 ~Abs_Tree();



	 std::vector< std::pair<Abs_Tree *, bool > > conditional_trees;

	 void build_abs_tree_exact(Conc_Tree * tree, std::vector<mem_regions_t *> &mem_regions);
	 void build_abs_tree_unrolled(Conc_Tree * tree, std::vector<mem_regions_t *> &mem_regions);
	 void seperate_intermediate_buffers();

	 bool is_tree_recursive();
	 Abs_Node * find_indirect_node(Abs_Node * node);
	 
	 std::vector<Abs_Node *> collect_input_nodes();

	 std::string serialize_tree();
	 void construct_tree(std::string stree);

	 void tag_parameters();
	 vector<Abs_Node *> retrieve_input_nodes();
	 vector<Abs_Node *> retrieve_parameters();

	 vector<Abs_Node *> get_buffer_region_nodes();

	 uint32_t get_maximum_dimensions();

	 void print_dot_algebraic(std::ostream &file, std::string name, uint32_t number, std::vector<std::string> vars);
	 

 };


 /* compound abs tree */
 class Comp_Abs_Tree : public Tree {

 private:
	 static void build_compound_tree_unrolled(Comp_Abs_Node *comp_node, std::vector<Abs_Node *> abs_nodes);
	 void traverse_trees(std::vector<Abs_Node *> abs_nodes, 
		 std::vector< std::pair < std::vector<Abs_Node *>, std::vector<int> > > &node_pos);

 public:

	 Comp_Abs_Tree();
	 ~Comp_Abs_Tree();
	 
	 void build_compound_tree_exact(std::vector<Abs_Tree *> abs_trees);
	 void build_compound_tree_unrolled(std::vector<Abs_Tree *> abs_trees);
	 void abstract_buffer_indexes();
	 Abs_Tree * compound_to_abs_tree();

	 std::string serialize_tree();
	 void construct_tree(std::string stree);
 };





#endif