#include <Windows.h>
#include "imageinfo.h"
#include "meminfo.h"
#include "moduleinfo.h"
#include "utilities.h"
#include "filter_logic.h"
#include "common_defines.h"
#include "memlayout.h"
#include <algorithm>

using namespace std;

#define DIFF_MODE		1
#define TWO_IMAGE_MODE  2
#define ONE_IMAGE_MODE	3

bool debug = false;
uint32_t debug_level = 0;
ofstream log_file;


void print_usage(){
	printf("usage - format -<name> <value>\n");
	printf("\t exec - the executable which DR analyzed (with exe\EXE) \n");
	printf("\t in_image - the in_image filename with ext \n");
	printf("\t out_image - the out_image filename with ext \n");
	printf("\t debug - 1,0 which turns debug mode on/off \n");
	printf("\t debug_level - the level of debugging (higher means more debug info) \n");
	printf("\t mode - mode of filtering \n");
	printf("\t total_size - size of the buffer\n");
	printf("\t threshold - continuous chunck % of image\n");
}


int main(int argc, char **argv){

	string process_name(argv[0]);
	int32_t mode = -1;
	vector<string> in_images;
	vector<string> out_images;
	string exec;
	uint threshold;
	uint total_size = 0;

	/*********************cmd line arg processing*************************/

	vector<cmd_args_t *> args = get_command_line_args(argc, argv);
	if (args.size() == 0){
		print_usage();
		exit(0);
	}

	for (int i = 0; i < args.size(); i++){
		if (args[i]->name.compare("-exec") == 0){
			exec = args[i]->value;
		}
		else if (args[i]->name.compare("-in_image") == 0){
			in_images.push_back(args[i]->value);
		}
		else if (args[i]->name.compare("-out_image") == 0){
			out_images.push_back(args[i]->value);
		}
		else if (args[i]->name.compare("-debug") == 0){
			debug = args[i]->value[0] - '0';
		}
		else if (args[i]->name.compare("-debug_level") == 0){
			debug_level = atoi(args[i]->value.c_str());
		}
		else if (args[i]->name.compare("-mode") == 0){
			mode = atoi(args[i]->value.c_str());
		}
		else if (args[i]->name.compare("-threshold") == 0){
			threshold = atoi(args[i]->value.c_str());
		}
		else if (args[i]->name.compare("-total_size") == 0){
			total_size = atoi(args[i]->value.c_str());
		}
		else{
			printf("unrecognized argument - %s\n", args[i]->name);
			exit(0);
		}
	
	}

	/*****************setting up files**************************/

	/*inputs*/
	vector<ifstream *> profile_files;
	vector<vector<ifstream *> > memtrace_files;
	vector<string> in_image_filenames;
	vector<string> out_image_filenames;

	/*get the files*/
	string output_folder = get_standard_folder("output");
	string image_folder = get_standard_folder("image");

	DEBUG_PRINT(("output folder - %s\n",output_folder.c_str()), 5);
	DEBUG_PRINT(("image folder - %s\n", image_folder.c_str()), 5);
	

	vector<string> files = get_all_files_in_folder(output_folder);

	for (int i = 0; i < in_images.size(); i++){
		string profile_string("profile_" + exec + "_" + in_images[i]);
		string memtrace_string("memtrace_" + exec + "_" + in_images[i]);

		DEBUG_PRINT(("profile string - %s\n", profile_string.c_str()), 5);
		DEBUG_PRINT(("memtrace string - %s\n", memtrace_string.c_str()), 5);

		vector<ifstream *> memtrace_per_image;
		for (int j = 0; j < files.size(); j++){
			if (is_prefix(files[j], profile_string)){
				DEBUG_PRINT(("profile - %s\n", (output_folder + "\\" + files[j]).c_str()), 5);
				ifstream * file = new ifstream(output_folder + "\\" + files[j], ifstream::in);
				profile_files.push_back(file);
			}
			else if (is_prefix(files[j], memtrace_string)){
				DEBUG_PRINT(("memtrace - %s\n", (output_folder + "\\" + files[j]).c_str()), 5);
				ifstream * file = new ifstream(output_folder + "\\" + files[j], ifstream::in);
				memtrace_per_image.push_back(file);
			}
		}
		memtrace_files.push_back(memtrace_per_image);
	}
	

	for (int i = 0; i < in_images.size(); i++){
		in_image_filenames.push_back(image_folder + "\\" + in_images[i]);
		out_image_filenames.push_back(image_folder + "\\" + out_images[i]);
	}

	/*outputs*/

	cout << process_name << endl;

	/*check whether process name has .exe or not*/
	size_t find = process_name.find(".exe");
	if (find != string::npos){
		process_name = process_name.substr(0, find);
	}

	ofstream filter_file(get_standard_folder("filter") + "\\" + process_name + "_" +  exec + ".log", ofstream::out); 
	ofstream app_pc_file(get_standard_folder("filter") + "\\" + process_name + "_" + exec + "_app_pc.log", ofstream::out);

	if (debug){
		log_file.open(get_standard_folder("log") + "\\" + process_name + "_" + exec + ".log", ofstream::out);
	}
	
	/****************************main algorithm***************************/

	/* DIFF_MODE */

	/*
	1. parse the file into moduleinfo_t structure 
	2. get the bb with the highest frequency
	3. get the func entry point (the deepest) for this bb
	4. parse memtrace files and extract pc_mem_regions
	5. link mem regions with consistent gaps and filter them based on the region size in comparison with the image sizes
	6. Rank pc_mem_regions based on size and get the pcs with the same functional composition as step 3
	7. printout the filter_funcs_file and app_pc_file

	optionally,

	1. printout the functional call stack recovered from the profile information

	*/


	ULONG_PTR token = initialize_image_subsystem();

	if (mode == DIFF_MODE){

		/* get the module information from the profile */
		DEBUG_PRINT(("populating module information....\n"), 1);
		moduleinfo_t * module = populate_moduleinfo(*profile_files[0]);
		DEBUG_PRINT( ("modules populated with profile information\n"), 1);

		/* get the image information */
		Gdiplus::Bitmap * in_image_bitmap = open_image(in_image_filenames[0].c_str());
		Gdiplus::Bitmap * out_image_bitmap = open_image(out_image_filenames[0].c_str());
		image_t * in_image = populate_imageinfo(in_image_bitmap);
		image_t * out_image = populate_imageinfo(out_image_bitmap);

		/* get the highest executed basic block */
		DEBUG_PRINT(("getting the highest executed basic block\n"), 1);
		
		moduleinfo_t * max_module = NULL;
		uint32_t max_start_addr;
		uint32_t max_addr;
		uint32_t max_freq = 0;
		uint32_t max_func = 0;

		moduleinfo_t * temp_module = module;
		while (temp_module != NULL){
			for (int i = 0; i < temp_module->funcs.size(); i++){
				funcinfo_t * func = temp_module->funcs[i];
				for (int j = 0; j < func->bbs.size(); j++){
					if (func->bbs[j]->freq > max_freq){
						max_freq = func->bbs[j]->freq;
						max_start_addr = func->bbs[j]->start_addr;
						max_module = temp_module;
					}
				}
			}
			temp_module = temp_module->next;
		}
		DEBUG_PRINT(("max module - %s, max start addr - %x\n", max_module->name, max_start_addr), 1);



		/* getting the func information for the highest executed bb */
		DEBUG_PRINT(("trying hard to get the probable func information\n"), 1);
		max_func = get_probable_func(module, max_module, max_start_addr);
		DEBUG_PRINT(("func - %x\n", max_func), 1);

		max_addr = max_start_addr;
		/* printing out the call stack */
		DEBUG_PRINT(("probable call stack information (upto 10 calls) for the given app_pc - %x\n", max_start_addr), 1);
		for (int i = 0; i < 5; i++){
			uint32_t func = get_probable_func(module, max_module, max_start_addr);
			DEBUG_PRINT(("func - %x %d\n", func, i), 1);
			if (func == 0) break;
			bbinfo_t * bbinfo = find_bb(max_module, func);
			DEBUG_PRINT((" - %d\n", bbinfo->freq), 1);
			if (bbinfo->from_bbs.size() > 0){
				max_start_addr = bbinfo->from_bbs[0]->target;
			}
			else{
				cout << "done : " << i << endl;
				break;
			}
		}
		

		/* parsing memtrace files to pc_mem_regions */

		/* get the memory region information -> link them together -> filter them */
		DEBUG_PRINT(("getting memory region information... \n"), 5);
		vector<pc_mem_region_t *> pc_mems = get_mem_regions_from_memtrace(memtrace_files[0], module);
		DEBUG_PRINT(("linking memory regions together... \n"), 5);
		link_mem_regions(pc_mems, GREEDY);  /* shouldn't this return the linking information? */
		DEBUG_PRINT(("filtering out insignificant regions... \n"), 5);
		
		/* all memory related information */
		vector<pc_mem_region_t *> total_mem_region = pc_mems;
		vector<mem_info_t *> total_mem_info = get_mem_info_from_memtrace(memtrace_files[0], module);
		link_mem_regions_greedy_dim(total_mem_info, 0);

		//print_mem_layout(log_file, pc_mems);

		if (total_size == 0){
			filter_mem_regions(pc_mems, in_image, out_image, threshold);
		}
		else{
			filter_mem_regions_total(pc_mems, total_size, threshold);
		}

		/* debug printing - this is after the filtering */
		print_mem_layout(log_file, pc_mems);
		vector<mem_info_t *> mems = extract_mem_regions(pc_mems);
		log_file << "********************extracted mems************************" << endl;
		print_mem_layout(log_file, mems);

		struct internal_func_info_t{
			string name;
			uint32_t addr;
			uint32_t freq;
			std::vector<uint32_t> candidate_instructions;
			std::vector<uint32_t> bb_start;

		};

		vector<internal_func_info_t *> func_info;
		
		/* get the pc_mems and there functional info as well as filter the pc_mems which are not in the func */
		for (int i = 0; i < pc_mems.size(); i++){
			DEBUG_PRINT(("entered finding funcs\n"), 1);
			moduleinfo_t * md = find_module(module, pc_mems[i]->module);
			ASSERT_MSG(md != NULL, ("ERROR: the module should be present\n"));
			bbinfo_t * bbinfo = find_bb(md, pc_mems[i]->pc);
			ASSERT_MSG(bbinfo != NULL, ("ERROR: bbinfo should be present\n"));
			DEBUG_PRINT(("finding func start for %x in bb %x\n", pc_mems[i]->pc, bbinfo->start_addr), 1);
			uint32_t func_start = get_probable_func(module, md, bbinfo->start_addr);
			if (func_start == 0) continue;
			DEBUG_PRINT(("module - %s, start - %x\n", pc_mems[i]->module.c_str(), func_start), 1);
			bool is_there = false;
			uint32_t index = 0;
			for (int j = 0; j < func_info.size(); j++){
				if (func_info[j]->addr == func_start && func_info[j]->name == md->name){
					is_there = true;
					index = j;
					break;
				}
			}

			if (!is_there){
				internal_func_info_t * new_func = new  internal_func_info_t;
				new_func->name = md->name;
				new_func->addr = func_start;
				new_func->freq = 1;
				new_func->candidate_instructions.push_back(pc_mems[i]->pc);
				new_func->bb_start.push_back(bbinfo->start_addr);
				func_info.push_back(new_func);
			}
			else{
				func_info[index]->freq++;
				bool found = false;
				for (int j = 0; j < func_info[index]->candidate_instructions.size(); j++){
					if (pc_mems[i]->pc == func_info[index]->candidate_instructions[j]){
						found = true;
						break;
					}
				}
				if (!found){
					func_info[index]->candidate_instructions.push_back(pc_mems[i]->pc);
					func_info[index]->bb_start.push_back(bbinfo->start_addr);
				}
			}
		}

		/* sort the probable function locations */
		sort(func_info.rbegin(), func_info.rend(), 
			[](internal_func_info_t * first, internal_func_info_t * second)->bool{

			return first->freq < second->freq;

		});



		/* print out the output files - heristic we are taking the function with the most amount of candidate instructions */
		print_app_pc_file(app_pc_file, func_info[0]->candidate_instructions, func_info[0]->name);
		print_funcs_filter_file(filter_file, func_info[0]->name, func_info[0]->addr);

		/* print out the summary */
		cout << "**********************Summary of localization**************************************" << endl;

		/* print out the maximum executed basic block summary */
		cout << "1. maximum executed basic block summary" << endl;
		cout << " module name - " << max_module->name << endl;
		cout << " bb start addr - " << max_addr << endl;
		cout << " bb freq - " << max_freq << endl;
		cout << " enclosed function - " << max_func << endl;

		cout << "2. functions accessing candidate instructions " << endl;
		/*print out the function with the most number of candidate instructions */
		for (int i = 0; i < func_info.size(); i++){
			cout << i + 1 << " - function" << endl;
			cout << " func addr - " << func_info[i]->addr << endl;
			cout << " module name - " << func_info[i]->name << endl;
			cout << " amount of candidate instructions - " << func_info[i]->freq << endl;
			cout << " candidate instructions - " << endl;
			for (int j = 0; j < func_info[i]->candidate_instructions.size(); j++){
				cout << "\t" << func_info[i]->candidate_instructions[j] << " - " << func_info[i]->bb_start[j] << endl;
			}
		}
		
		//populate_function_addr(module);
		//moduleinfo_t * func_module = move_to_function_composition(module);
		//print_moduleinfo(log_file, func_module);

		
		


		
	}
	else if(mode == TWO_IMAGE_MODE){

	}
	else{

		//get the function composition
		/*vector<func_composition_t *> comp;
		if (mode == ONE_IMAGE_MODE){
		comp = filter_based_on_memory_dependancy(pc_mems, module);
		}
		else{
		if (is_funcs_present(module)){
		comp = create_func_composition_func(pc_mems, module);
		}
		else{
		comp = create_func_composition_wo_func(pc_mems, module);
		}
		}


		//crude heuristic
		//now get the function with the most number of app_pc s
		uint32_t max_pcs = 0;
		func_composition_t * func = NULL;
		for (int i = 0; i < comp.size(); i++){
		if (comp[i]->region.size() > max_pcs){
		func = comp[i];
		max_pcs = func->region.size();
		}
		}

		//print this function composition and the app_pcs involved in it
		print_filter_file(log_file, func);*/

	}
	
	shutdown_image_subsystem(token);

	/* delete the objects and close the files */


	return 0;

}











