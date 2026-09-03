/**
 * An implementation of an IPFS node
 * Copying the go-ipfs-node project
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "inttypes.h"

#include "mh/multihash.h"
#include "mh/hashes.h"
#include "ipfs/cid/cid.h"
#include "ipfs/merkledag/node.h"
#include "ipfs/unixfs/unixfs.h"

extern char *strtok_r(char *, const char *, char **);

// for protobuf Node (all fields optional)    data (optional bytes)      links (repeated node_link)
enum WireType ipfs_node_message_fields[] = { WIRETYPE_LENGTH_DELIMITED, WIRETYPE_LENGTH_DELIMITED };
// for protobuf NodeLink (all fields optional)       hash                            name                     tsize
enum WireType ipfs_node_link_message_fields[] = { WIRETYPE_LENGTH_DELIMITED, WIRETYPE_LENGTH_DELIMITED, WIRETYPE_VARINT };

/*====================================================================================
 * Link Functions
 *===================================================================================*/

/* ipfs_node_link_new
 * @Param name: The name of the link (char *)
 * @Param size: Size of the link (size_t)
 * @Param ahash: An Qmhash
 * @returns true(1) on success, false(0) otherwise
 */
int ipfs_node_link_create(char * name, unsigned char * ahash, size_t hash_size, struct NodeLink** node_link)
{
	ipfs_node_link_new(node_link);
	if (*node_link == NULL)
		return 0;

	struct NodeLink* link = *node_link;
	// hash
	link->hash_size = hash_size;
	link->hash = (unsigned char*)malloc(hash_size);
	if (link->hash == NULL) {
		ipfs_node_link_free(link);
		*node_link = NULL;
		return 0;
	}
	memcpy(link->hash, ahash, hash_size);
	// name
	if (name != NULL && strlen(name) > 0) {
		link->name = malloc(strlen(name) + 1);
		if ( link->name == NULL) {
			ipfs_node_link_free(link);
			*node_link = NULL;
			return 0;
		}
		strcpy(link->name, name);
	}
	// t_size
	link->t_size = 0;
	// other, non-protobuffed data
	link->next = NULL;

	return 1;
}

int ipfs_node_link_new(struct NodeLink** node_link) {
	*node_link = malloc(sizeof(struct NodeLink));
	if (*node_link == NULL)
		return 0;

	struct NodeLink* link = *node_link;
	link->hash = NULL;
	link->hash_size = 0;
	link->name = NULL;
	link->next = NULL;
	link->t_size = 0;
	return 1;
}

/* ipfs_node_link_free
 * @param node_link: Free the link you have allocated.
 */
int ipfs_node_link_free(struct NodeLink * node_link)
{
	if (node_link != NULL) {
		if (node_link->hash != NULL)
			free(node_link->hash);
		if (node_link->name != NULL)
			free(node_link->name);
		free(node_link);
	}
	return 1;
}

/***
 * Find length of encoded version of NodeLink
 * @param link the link to examine
 * @returns the maximum length of the encoded NodeLink
 */
size_t ipfs_node_link_protobuf_encode_size(const struct NodeLink* link) {
	if (link == NULL)
		return 0;
	// hash, name, tsize
	size_t size = 0;
	if (link->hash_size > 0) {
		size += 11 + link->hash_size;
	}
	if (link->name != NULL && strlen(link->name) > 0) {
		size += 11 + strlen(link->name);
	}
	if (link->t_size > 0) {
		size += 22;
	}
	return size;
}

/**
 * Encode a NodeLink in protobuf format
 * @param link the link to work with
 * @param buffer the buffer to fill
 * @param max_buffer_length the length of the buffer
 * @param bytes_written the number of bytes written to buffer
 * @returns true(1) on success
 */
int ipfs_node_link_protobuf_encode(const struct NodeLink* link, unsigned char* buffer, size_t max_buffer_length, size_t* bytes_written) {
	// 3 fields, hash (length delimited), name (length delimited), tsize (varint)
	size_t bytes_used = 0;
	int retVal = 0;
	*bytes_written = 0;
	// hash
	if (link->hash_size > 0) {
		size_t hash_length = mh_new_length(MH_H_SHA2_256, link->hash_size);
		unsigned char hash[hash_length];
		mh_new(hash, MH_H_SHA2_256, link->hash, link->hash_size);
		retVal = protobuf_encode_length_delimited(1, ipfs_node_link_message_fields[0], (char*)hash, hash_length, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0) {
			return 0;
		}
		*bytes_written += bytes_used;
	}
	// name is optional, but they encode it anyways
	if (link->name != NULL && strlen(link->name) > 0) {
		retVal = protobuf_encode_string(2, ipfs_node_link_message_fields[1], link->name, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0)
			return 0;
		*bytes_written += bytes_used;
	} else {
		retVal = protobuf_encode_string(2, ipfs_node_link_message_fields[1], "", &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0)
			return 0;
		*bytes_written += bytes_used;
	}
	// tsize
	if (link->t_size > 0) {
		retVal = protobuf_encode_varint(3, ipfs_node_link_message_fields[2], link->t_size, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0)
			return 0;
		*bytes_written += bytes_used;
	}
	return 1;
}

/**
 * Turn a protobuf encoded byte array into a NodeLink object
 * @param buffer the buffer to look in
 * @param buffer_length the length of the buffer
 * @param link the link to fill
 * @returns true(1) on success
 */
int ipfs_node_link_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct NodeLink** node_link) {
	size_t pos = 0;
	int retVal = 0;
	struct NodeLink* link = NULL;

	// allocate memory for object
	if (ipfs_node_link_new(node_link) == 0)
		goto exit;

	link = *node_link;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			goto exit;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): { // hash
				size_t hash_size = 0;
				unsigned char* hash;
				if (protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&hash, &hash_size, &bytes_read) == 0)
					goto exit;
				if (hash_size < 2) {
					goto exit;
				}
				link->hash_size = hash_size - 2;
				link->hash = (unsigned char*)malloc(link->hash_size);
				if (link->hash == NULL) {
					goto exit;
				}
				memcpy((char*)link->hash, (char*)&hash[2], link->hash_size);
				free(hash);
				pos += bytes_read;
				break;
			}
			case (2): { // name
				if (protobuf_decode_string(&buffer[pos], buffer_length - pos, &link->name, &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				break;
			}
			case (3): { // t_size
				if (protobuf_decode_varint(&buffer[pos], buffer_length - pos, (unsigned long long*)&link->t_size, &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				break;
			}
		}
	}

	retVal = 1;

exit:
	if (retVal == 0) {
		if (link != NULL)
			ipfs_node_link_free(link);
	}

	return retVal;
}

static int node_link_compare(const void* a, const void* b)
{
	const struct NodeLink* link_a = *(const struct NodeLink**)a;
	const struct NodeLink* link_b = *(const struct NodeLink**)b;

	const char* name_a = link_a->name != NULL ? link_a->name : "";
	const char* name_b = link_b->name != NULL ? link_b->name : "";
	int name_cmp = strcmp(name_a, name_b);
	if (name_cmp != 0)
		return name_cmp;

	size_t min_len = link_a->hash_size < link_b->hash_size ? link_a->hash_size : link_b->hash_size;
	int hash_cmp = memcmp(link_a->hash, link_b->hash, min_len);
	if (hash_cmp != 0)
		return hash_cmp;

	return (int)(link_a->hash_size - link_b->hash_size);
}

static size_t ipfs_hashtable_node_link_count(const struct HashtableNode* node)
{
	size_t count = 0;
	struct NodeLink* current = node->head_link;
	while (current != NULL) {
		count++;
		current = current->next;
	}
	return count;
}

/***
 * return an approximate size of the encoded node
 */
size_t ipfs_hashtable_node_protobuf_encode_size(const struct HashtableNode* node) {
	size_t size = 0;
	// links
	struct NodeLink* current = node->head_link;
	while(current != NULL) {
		size += 11 + ipfs_node_link_protobuf_encode_size(current);
		current = current->next;
	}
	// data
	if (node->data_size > 0) {
		size += 11 + node->data_size;
	}
	return size;
}

/***
 * Encode a node into a protobuf byte stream
 * @param node the node to encode
 * @param buffer where to put it
 * @param max_buffer_length the length of buffer
 * @param bytes_written how much of buffer was used
 * @returns true(1) on success
 */
int ipfs_hashtable_node_protobuf_encode(const struct HashtableNode* node, unsigned char* buffer, size_t max_buffer_length, size_t* bytes_written) {
	size_t bytes_used = 0;
	*bytes_written = 0;
	int retVal = 0;

	// Collect and sort links for deterministic encoding
	size_t link_count = ipfs_hashtable_node_link_count(node);
	struct NodeLink** sorted_links = NULL;
	if (link_count > 0) {
		sorted_links = (struct NodeLink**)malloc(link_count * sizeof(struct NodeLink*));
		if (sorted_links == NULL)
			return 0;
		struct NodeLink* current = node->head_link;
		for (size_t i = 0; i < link_count && current != NULL; i++) {
			sorted_links[i] = current;
			current = current->next;
		}
		qsort(sorted_links, link_count, sizeof(struct NodeLink*), node_link_compare);
	}

	// encode sorted links
	for (size_t i = 0; i < link_count; i++) {
		size_t temp_size = ipfs_node_link_protobuf_encode_size(sorted_links[i]);
		unsigned char temp[temp_size];
		retVal = ipfs_node_link_protobuf_encode(sorted_links[i], temp, temp_size, &bytes_used);
		if (retVal == 0) {
			free(sorted_links);
			return 0;
		}
		retVal = protobuf_encode_length_delimited(2, ipfs_node_message_fields[1], (char*)temp, bytes_used, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0) {
			free(sorted_links);
			return 0;
		}
		*bytes_written += bytes_used;
	}
	if (sorted_links != NULL)
		free(sorted_links);

	// data
	if (node->data_size > 0) {
		retVal = protobuf_encode_length_delimited(1, ipfs_node_message_fields[0], (char*)node->data, node->data_size, &buffer[*bytes_written], max_buffer_length - *bytes_written, &bytes_used);
		if (retVal == 0)
			return 0;
		*bytes_written += bytes_used;
	}

	return 1;
}

/***
 * Decode a stream of bytes into a Node structure
 * @param buffer where to get the bytes from
 * @param buffer_length the length of buffer
 * @param node pointer to the Node to be created
 * @returns true(1) on success
 */
int ipfs_hashtable_node_protobuf_decode(unsigned char* buffer, size_t buffer_length, struct HashtableNode** node) {
	/*
	 * Field 1: data
	 * Field 2: link
	 */
	size_t pos = 0;
	int retVal = 0;
	unsigned char* temp_buffer = NULL;
	size_t temp_size;
	struct NodeLink* temp_link = NULL;

	if (buffer_length == 0)
		goto exit;

	if (ipfs_hashtable_node_new(node) == 0)
		goto exit;

	while(pos < buffer_length) {
		size_t bytes_read = 0;
		int field_no;
		enum WireType field_type;
		if (protobuf_decode_field_and_type(&buffer[pos], buffer_length, &field_no, &field_type, &bytes_read) == 0) {
			goto exit;
		}
		pos += bytes_read;
		switch(field_no) {
			case (1): { // data
				if (protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&((*node)->data), &((*node)->data_size), &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				break;
			}
			case (2): {// links
				if (protobuf_decode_length_delimited(&buffer[pos], buffer_length - pos, (char**)&temp_buffer, &temp_size, &bytes_read) == 0)
					goto exit;
				pos += bytes_read;
				if (ipfs_node_link_protobuf_decode(temp_buffer, temp_size, &temp_link)) {
					ipfs_hashtable_node_add_link(*node, temp_link);
				}
				free(temp_buffer);
				temp_buffer = NULL;
				break;
			}
		}
	}

	retVal = 1;

exit:
	if (retVal == 0) {
		ipfs_hashtable_node_free(*node);
	}
	if (temp_buffer != NULL)
		free(temp_buffer);

	return retVal;
}

/*====================================================================================
 * Node Functions
 *===================================================================================*/
/*ipfs_node_new
 * Creates an empty node, allocates the required memory
 * Returns a fresh new node with no data set in it.
 */
int ipfs_hashtable_node_new(struct HashtableNode** node)
{
	*node = (struct HashtableNode *)malloc(sizeof(struct HashtableNode));
	if (*node == NULL)
		return 0;
	(*node)->hash = NULL;
	(*node)->hash_size = 0;
	(*node)->data = NULL;
	(*node)->data_size = 0;
	(*node)->encoded = NULL;
	(*node)->head_link = NULL;
	return 1;
}

/***
 * Allocates memory for a node, and sets the data section to indicate
 * that this node is a directory
 * @param node the node to initialize
 * @returns true(1) on success, otherwise false(0)
 */
int ipfs_hashtable_node_create_directory(struct HashtableNode** node) {
	// initialize parent_node
	if (ipfs_hashtable_node_new(node) == 0)
		return 0;
	// put a UnixFS protobuf in the data section
	struct UnixFS* unix_fs;
	if (ipfs_unixfs_new(&unix_fs) == 0) {
		ipfs_hashtable_node_free(*node);
		return 0;
	}
	unix_fs->data_type = UNIXFS_DIRECTORY;
	size_t protobuf_len = ipfs_unixfs_protobuf_encode_size(unix_fs);
	unsigned char protobuf[protobuf_len];
	if (ipfs_unixfs_protobuf_encode(unix_fs, protobuf, protobuf_len, &protobuf_len) == 0) {
		ipfs_hashtable_node_free(*node);
		ipfs_unixfs_free(unix_fs);
		return 0;
	}
	ipfs_unixfs_free(unix_fs);
	ipfs_hashtable_node_set_data(*node, protobuf, protobuf_len);
	return 1;
}

int ipfs_hashtable_node_is_directory(struct HashtableNode* node) {
	if (node->data_size < 2) {
		return 0;
	}
	struct UnixFS* unix_fs;
	if (ipfs_unixfs_protobuf_decode(node->data, node->data_size, &unix_fs) == 0) {
		return 0;
	}
	int retVal = (unix_fs->data_type == UNIXFS_DIRECTORY);
	ipfs_unixfs_free(unix_fs);
	return retVal;
}

/**
 * Set the cached struct element
 * @param node the node to be modified
 * @param cid the Cid to be copied into the Node->cached element
 * @returns true(1) on success
 */
int ipfs_hashtable_node_set_hash(struct HashtableNode* node, const unsigned char* hash, size_t hash_size)
{
	// don't reallocate if it is the same size
	if (node->hash != NULL && hash_size != node->hash_size) {
		free(node->hash);
		node->hash = NULL;
		node->hash_size = 0;
	}
	// we must reallocate
	if (node->hash == NULL && hash_size > 0) {
		node->hash = (unsigned char*)malloc(hash_size);
		if (node->hash == NULL)
			return 0;
	}
	if (hash_size > 0) { // don't bother if there is nothing to copy
		memcpy(node->hash, hash, hash_size);
		node->hash_size = hash_size;
	}
	return 1;
}

/*ipfs_node_set_data
 * Sets the data of a node
 * @param Node: The node which you want to set data in.
 * @param Data, the data you want to assign to the node
 * Sets pointers of encoded & cached to NULL /following go method
 * returns 1 on success 0 on failure
 */
int ipfs_hashtable_node_set_data(struct HashtableNode* node, unsigned char * Data, size_t data_size)
{
	if(!node || !Data)
	{
		return 0;
	}
	if (node->data != NULL) {
		free(node->data);
	}
	node->data = malloc(sizeof(unsigned char) * data_size);
	if (node->data == NULL)
		return 0;

	memcpy(node->data, Data, data_size);
	node->data_size = data_size;
	return 1;
}

/*ipfs_node_set_encoded
 * @param NODE: the node you wish to alter (struct Node *)
 * @param Data: The data you wish to set in encoded.(unsigned char *)
 * returns 1 on success 0 on failure
 */
int ipfs_hashtable_node_set_encoded(struct HashtableNode * N, unsigned char * Data)
{
	if(!N || !Data)
	{
		return 0;
	}
	N->encoded = Data;
	//I don't know if these will be needed, enable them if you need them.
	//N->cached = NULL;
	//N->data = NULL;
	return 1;
}
/*ipfs_node_get_data
 * Gets data from a node
 * @param Node: = The node you want to get data from. (unsigned char *)
 * Returns data of node.
 */
unsigned char * ipfs_hashtable_node_get_data(struct HashtableNode * N)
{
	unsigned char * DATA;
	DATA = N->data;
	return DATA;
}

struct NodeLink* ipfs_node_link_last(struct HashtableNode* node) {
	struct NodeLink* current = node->head_link;
	while(current != NULL) {
		if (current->next == NULL)
			break;
		current = current->next;
	}
	return current;
}

int ipfs_node_remove_link(struct HashtableNode* node, struct NodeLink* toRemove) {
	struct NodeLink* current = node->head_link;
	struct NodeLink* previous = NULL;
	while(current != NULL && current != toRemove) {
		previous = current;
		current = current->next;
	}
	if (current != NULL) {
		if (previous == NULL) {
			// removing the head of the list
			node->head_link = current->next;
		} else {
			previous->next = current->next;
		}
		ipfs_node_link_free(current);
		return 1;
	}
	return 0;
}

/*ipfs_node_free
 * Once you are finished using a node, always delete it using this.
 * It will take care of the links inside it.
 * @param N: the node you want to free. (struct Node *)
 */
int ipfs_hashtable_node_free(struct HashtableNode * N)
{
	if(N != NULL)
	{
		// remove links
		struct NodeLink* current = N->head_link;
		while (current != NULL) {
			current = current->next;
			ipfs_node_link_free(N->head_link);
			N->head_link = current;
		}
		N->head_link = NULL;
		if(N->hash != NULL)
		{
			free(N->hash);
			N->hash = NULL;
			N->hash_size = 0;
		}
		if (N->data) {
			free(N->data);
			N->data = NULL;
			N->data_size = 0;
		}
		if (N->encoded != NULL) {
			free(N->encoded);
		}
		free(N);
		N = NULL;
	}
	return 1;
}

/*ipfs_node_get_link_by_name
 * Returns a copy of the link with given name
 * @param Name: (char * name) searches for link with this name
 * Returns the link struct if it's found otherwise returns NULL
 */
struct NodeLink * ipfs_hashtable_node_get_link_by_name(struct HashtableNode * N, char * Name)
{
	struct NodeLink* current = N->head_link;
	while(current != NULL && strcmp(Name, current->name) != 0) {
		current = current->next;
	}
	return current;
}

/*ipfs_node_remove_link_by_name
 * Removes a link from node if found by name.
 * @param name: Name of link (char * name)
 * returns 1 on success, 0 on failure.
 */
int ipfs_hashtable_node_remove_link_by_name(char * Name, struct HashtableNode * mynode)
{
	struct NodeLink* current = mynode->head_link;
	struct NodeLink* previous = NULL;
	while( (current != NULL)
			&& (( Name == NULL && current->name != NULL )
			|| ( Name != NULL && current->name == NULL )
			|| ( Name != NULL && current->name != NULL && strcmp(Name, current->name) != 0) ) ) {
		previous = current;
		current = current->next;
	}
	if (current != NULL) {
		// we found it
		if (previous == NULL) {
			// we're first, use the next one (if there is one)
			if (current->next != NULL)
				mynode->head_link = current->next;
		} else {
			// we're somewhere in the middle, remove me from the list
			previous->next = current->next;
			ipfs_node_link_free(current);
		}

		return 1;
	}
	return 0;
}

/* ipfs_node_add_link
 * Adds a link to your node
 * @param node the node to add to
 * @param mylink: the link to add
 * @returns true(1) on success
 */
int ipfs_hashtable_node_add_link(struct HashtableNode* node, struct NodeLink * mylink)
{
	if(node->head_link != NULL) {
		// add to existing by finding last one
		struct NodeLink* current_end = node->head_link;
		while(current_end->next != NULL) {
			current_end = current_end->next;
		}
		// now we have the last one, add to it
		current_end->next = mylink;
	}
	else
	{
		node->head_link = mylink;
	}
	return 1;
}

/*ipfs_node_new_from_link
 * Create a node from a link
 * @param mylink: the link you want to create it from. (struct Cid *)
 * @param linksize: sizeof(the link in mylink) (size_T)
 * Returns a fresh new node with the link you specified. Has to be freed with Node_Free preferably.
 */
int ipfs_hashtable_node_new_from_link(struct NodeLink * mylink, struct HashtableNode** node)
{
	*node = (struct HashtableNode *) malloc(sizeof(struct HashtableNode));
	if (*node == NULL)
		return 0;
	(*node)->head_link = NULL;
	ipfs_hashtable_node_add_link(*node, mylink);
	(*node)->hash = NULL;
	(*node)->hash_size = 0;
	(*node)->data = NULL;
	(*node)->data_size = 0;
	(*node)->encoded = NULL;
	return 1;
}

/**
 * create a new Node struct with data
 * @param data: bytes buffer you want to create the node from
 * @param data_size the size of the data buffer
 * @param node a pointer to the node to be created
 * returns a node with the data you inputted.
 */
int ipfs_hashtable_node_new_from_data(unsigned char * data, size_t data_size, struct HashtableNode** node)
{
	if(data)
	{
		if (ipfs_hashtable_node_new(node) == 0)
			return 0;
		return ipfs_hashtable_node_set_data(*node, data, data_size);
	}
	return 0;
}

/***
 * create a Node struct from encoded data
 * @param data: encoded bytes buffer you want to create the node from. Note: this copies the pointer, not a memcpy
 * @param node a pointer to the node that will be created
 * @returns true(1) on success
 */
int ipfs_hashtable_node_new_from_encoded(unsigned char * data, struct HashtableNode** node)
{
	if(data)
	{
		if (ipfs_hashtable_node_new(node) == 0)
			return 0;
		(*node)->encoded = data;
		return 1;
	}
	return 0;
}
/*Node_Resolve_Max_Size
 * !!!This shouldn't concern you!
 *Gets the ammount of words that will be returned by Node_Resolve
 *@Param1: The string that will be processed (eg: char * sentence = "foo/bar/bin")
 *Returns either -1 if something went wrong or the ammount of words that would be processed.
*/
int Node_Resolve_Max_Size(char * input1)
{
	if(!input1)
	{
		return -1; // Input is null, therefor nothing can be processed.
	}
	char input[strlen(input1)+1];
	bzero(input, strlen(input1));
	strcpy(input, input1);
	int num = 0;
	char * tr;
	char * end;
	tr=strtok_r(input,"/",&end);
	for(int i = 0;tr;i++)
	{
		tr=strtok_r(NULL,"/",&end);
		num++;
	}
	return num;
}

/*Node_Resolve Basically stores everything in a pointer array eg: char * bla[Max_Words_]
 * !!!This shouldn't concern you!!!
 *@param1: Pointer array(char * foo[x], X=Whatever ammount there is. should be used with the helper function Node_Resolve_Max_Size)
 *@param2: Sentence to gather words/paths from (Eg: char * meh = "foo/bar/bin")
 *@Returns 1 or 0, 0 if something went wrong, 1 if everything went smoothly.
*/

int Node_Resolve(char ** result, char * input1)
{
	if(!input1)
	{
		return 0; // Input is null, therefor nothing can be processed.
	}
	char input[strlen(input1)+1];
	bzero(input, strlen(input1));
	strcpy(input, input1);
	char * tr;
	char * end;
	tr=strtok_r(input,"/",&end);
	int retVal = 1;
	for(int i = 0;tr;i++)
	{
		result[i] = (char *) malloc(strlen(tr)+1);
		if (result[i] != NULL) {
			strcpy(result[i], tr);
		} else {
			retVal = 0;
		}
		tr=strtok_r(NULL,"/",&end);
	}
	return retVal;
}

/*Node_Resolve_Links
 * Processes a path returning all links.
 * @param N: The node you want to get links from
 * @param path: The "foo/bar/bin" path
 */
struct Link_Proc * Node_Resolve_Links(struct HashtableNode * N, char * path)
{
	if(!N || !path)
	{
		return NULL;
	}
	int expected_link_ammount = Node_Resolve_Max_Size(path);
	struct Link_Proc * LProc = (struct Link_Proc *) malloc(sizeof(struct Link_Proc) + sizeof(struct NodeLink) * expected_link_ammount);
	if (LProc == NULL) {
		return NULL;
	}
	LProc->ammount = 0;
	char * linknames[expected_link_ammount];
	Node_Resolve(linknames, path);
	for(int i=0;i<expected_link_ammount; i++)
	{
		struct NodeLink * proclink;
		proclink = ipfs_hashtable_node_get_link_by_name(N, linknames[i]);
		if(proclink)
		{
			LProc->links[i] = (struct NodeLink *)malloc(sizeof(struct NodeLink));
			if (LProc->links[i] != NULL) {
				memcpy(LProc->links[i], proclink, sizeof(struct NodeLink));
				LProc->ammount++;
			} else {
				// Allocation failed: unwind previously allocated links
				for (int j = 0; j < i; j++) {
					if (LProc->links[j]) {
						free(LProc->links[j]);
						LProc->links[j] = NULL;
					}
				}
				LProc->ammount = 0;
				free(proclink);
				for(int k=0;k<expected_link_ammount; k++) {
					free(linknames[k]);
				}
				free(LProc);
				return NULL;
			}
			free(proclink);
		}
	}
	//Freeing pointer array
	for(int i=0;i<expected_link_ammount; i++)
	{
		free(linknames[i]);
	}
	return LProc;
}
/*Free_link_Proc
 * frees the Link_Proc struct you created.
 * @param1: Link_Proc struct (struct Link_Proc *)
 */
void Free_Link_Proc(struct Link_Proc * LPRC)
{
	if(LPRC->ammount != 0)
	{
		for(int i=0;i<LPRC->ammount;i++)
		{
			ipfs_node_link_free(LPRC->links[i]);
		}
	}
	free(LPRC);
}

/*Node_Tree() Basically a unix-like ls
 *@Param1: Result char * foo[strlen(sentence)]
 *@Param2: char sentence[] = foo/bar/bin/whatever
 *Return: 0 if failure, 1 if success
*/
int Node_Tree(char * result, char * input1) //I don't know where you use this but here it is.
{
	if(!input1)
	{
		return 0;
	}
	char input[strlen(input1)+1];
	bzero(input, strlen(input1));
	strcpy(input, input1);
	for(int i=0; i<strlen(input); i++)
	{
		if(input[i] == '/')
		{
			input[i] = ' ';
		}
	}
	strcpy(result, input);
	return 1;
}
