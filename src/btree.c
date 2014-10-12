#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "rlite.h"
#include "btree.h"
#include "util.h"

int rl_print_btree_node(rl_btree *btree, rl_btree_node *node, long level);
int rl_btree_node_create(rl_btree *btree, rl_btree_node **node);

int long_cmp(void *v1, void *v2)
{
	long a = *((long *)v1), b = *((long *)v2);
	if (a == b) {
		return 0;
	}
	return a > b ? 1 : -1;
}

int long_set_btree_node_serialize(rl_btree *btree, rl_btree_node *node, unsigned char **_data, long *data_size)
{
	unsigned char *data = malloc(sizeof(unsigned char) * ((8 * btree->max_node_size) + 8));
	if (!data) {
		return RL_OUT_OF_MEMORY;
	}
	put_4bytes(data, node->size);
	long i, pos = 4;
	for (i = 0; i < node->size; i++) {
		put_4bytes(&data[pos], *(long *)(node->scores[i]));
		put_4bytes(&data[pos + 4], node->children ? node->children[i] : 0);
		pos += 8;
	}
	put_4bytes(&data[pos], node->children ? node->children[node->size] : 0);
	*_data = data;
	if (data_size) {
		*data_size = pos + 4;
	}
	return RL_OK;
}

int long_set_btree_node_deserialize(rl_btree *btree, unsigned char *data, rl_btree_node **_node)
{
	rl_btree_node *node;
	long i, pos = 4, child;
	int retval;
	retval = rl_btree_node_create(btree, &node);
	if (retval != RL_OK) {
		goto cleanup;
	}
	node->size = (long)get_4bytes(data);
	for (i = 0; i < node->size; i++) {
		node->scores[i] = malloc(sizeof(long));
		if (!node->scores[i]) {
			node->size = i;
			retval = RL_OUT_OF_MEMORY;
			goto cleanup;
		}
		*(long *)node->scores[i] = get_4bytes(&data[pos]);
		child = get_4bytes(&data[pos + 4]);
		if (child != 0) {
			if (!node->children) {
				node->children = malloc(sizeof(long) * (btree->max_node_size + 1));
				if (!node->children) {
					free(node->scores[i]);
					node->size = i;
					retval = RL_OUT_OF_MEMORY;
					goto cleanup;
				}
			}
			node->children[i] = child;
		}
		pos += 8;
	}
	child = get_4bytes(&data[pos]);
	if (child != 0) {
		node->children[node->size] = child;
	}
	*_node = node;
cleanup:
	if (retval != RL_OK && node) {
		rl_btree_node_destroy(btree, node);
	}
	return retval;
}

int long_hash_btree_node_serialize(rl_btree *btree, rl_btree_node *node, unsigned char **_data, long *data_size)
{
	int retval = RL_OK;
	unsigned char *data = malloc(sizeof(unsigned char) * (((btree->type->score_size + btree->type->value_size + 4) * btree->max_node_size) + 8));
	if (!data) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	long i, pos = 4;
	put_4bytes(data, node->size);
	for (i = 0; i < node->size; i++) {
		put_4bytes(&data[pos], *(long *)(node->scores[i]));
		put_4bytes(&data[pos + 4], node->children ? node->children[i] : 0);
		put_4bytes(&data[pos + 8], *(long *)node->values[i]);
		pos += 12;
	}
	put_4bytes(&data[pos], node->children ? node->children[node->size] : 0);
	*_data = data;
	if (data_size) {
		*data_size = pos + 4;
	}
cleanup:
	return retval;
}

int long_hash_btree_node_deserialize(rl_btree *btree, unsigned char *data, rl_btree_node **_node)
{
	rl_btree_node *node;
	long i, pos = 4, child;
	int retval = rl_btree_node_create(btree, &node);
	if (retval != RL_OK) {
		goto cleanup;
	}
	node->size = (long)get_4bytes(data);
	for (i = 0; i < node->size; i++) {
		node->scores[i] = malloc(sizeof(long));
		if (!node->scores[i]) {
			node->size = i;
			retval = RL_OUT_OF_MEMORY;
			goto cleanup;
		}
		*(long *)node->scores[i] = get_4bytes(&data[pos]);
		child = get_4bytes(&data[pos + 4]);
		if (child != 0) {
			if (!node->children) {
				node->children = malloc(sizeof(long) * (btree->max_node_size + 1));
				if (!node->children) {
					free(node->scores[i]);
					node->size = i;
					retval = RL_OUT_OF_MEMORY;
					goto cleanup;
				}
			}
			node->children[i] = child;
		}
		node->values[i] = malloc(sizeof(long));
		if (!node->values[i]) {
			free(node->scores[i]);
			node->size = i;
			retval = RL_OUT_OF_MEMORY;
			goto cleanup;
		}
		*(long *)node->values[i] = get_4bytes(&data[pos + 8]);
		pos += 12;
	}
	child = get_4bytes(&data[pos]);
	if (child != 0) {
		node->children[node->size] = child;
	}
	*_node = node;
cleanup:
	if (retval != RL_OK && node) {
		rl_btree_node_destroy(btree, node);
	}
	return retval;
}

int long_formatter(void *v2, char **formatted, int *size)
{
	*formatted = malloc(sizeof(char) * 22);
	if (*formatted == NULL) {
		return RL_OUT_OF_MEMORY;
	}
	*size = snprintf(*formatted, 22, "%ld", *(long *)v2);
	return RL_OK;
}

void init_long_set()
{
	long_set.score_size = sizeof(long);
	long_set.value_size = 0;
	long_set.cmp = long_cmp;
	long_set.formatter = long_formatter;
	long_set.serialize = long_set_btree_node_serialize;
	long_set.deserialize = long_set_btree_node_deserialize;
}

void init_long_hash()
{
	long_hash.score_size = sizeof(long);
	long_hash.value_size = sizeof(void *);
	long_hash.cmp = long_cmp;
	long_hash.formatter = long_formatter;
	long_hash.serialize = long_hash_btree_node_serialize;
	long_hash.deserialize = long_hash_btree_node_deserialize;
}

int rl_btree_node_create(rl_btree *btree, rl_btree_node **_node)
{
	int retval = RL_OK;
	rl_btree_node *node = malloc(sizeof(rl_btree_node));
	if (!node) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	node->scores = malloc(sizeof(void *) * btree->max_node_size);
	if (!node->scores) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	node->children = NULL;
	if (btree->type->value_size) {
		node->values = malloc(sizeof(void *) * btree->max_node_size);
		if (!node->values) {
			retval = RL_OUT_OF_MEMORY;
			goto cleanup;
		}
	}
	else {
		node->values = NULL;
	}
	node->size = 0;
	*_node = node;
cleanup:
	if (retval != RL_OK && node) {
		if (node->scores) {
			free(node->scores);
		}
		free(node);
	}
	return retval;
}

int rl_btree_node_destroy(rl_btree *btree, rl_btree_node *node)
{
	btree = btree;
	long i;
	if (node->scores) {
		for (i = 0; i < node->size; i++) {
			free(node->scores[i]);
		}
		free(node->scores);
	}
	if (node->values) {
		for (i = 0; i < node->size; i++) {
			free(node->values[i]);
		}
		free(node->values);
	}
	if (node->children) {
		free(node->children);
	}
	free(node);
	return RL_OK;
}

int rl_btree_create(rl_btree **_btree, rl_btree_type *type, long max_node_size, rl_accessor *accessor)
{
	int retval = RL_OK;
	rl_btree *btree = malloc(sizeof(rl_btree));
	rl_btree_node *root = NULL;
	if (!btree) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	btree->max_node_size = max_node_size;
	btree->type = type;
	btree->accessor = accessor;
	btree->height = 1;
	retval = rl_btree_node_create(btree, &root);
	if (retval != RL_OK) {
		goto cleanup;
	}
	root->size = 0;
	retval = btree->accessor->insert(btree, &btree->root, root);
	if (retval != RL_OK) {
		goto cleanup;
	}
	*_btree = btree;
cleanup:
	if (retval != RL_OK && btree) {
		if (root) {
			free(root);
		}
		free(btree);
	}
	return retval;
}

int rl_btree_destroy(rl_btree *btree)
{
	int retval = RL_OK;
	long i;
	rl_btree_node **nodes;
	long size;
	retval = btree->accessor->list(btree, &nodes, &size);
	if (retval != RL_OK) {
		goto cleanup;
	}
	for (i = 0; i < size; i++) {
		retval = rl_btree_node_destroy(btree, nodes[i]);
		if (retval != RL_OK) {
			goto cleanup;
		}
	}
	free(btree);
cleanup:
	return retval;
}

int rl_btree_find_score(rl_btree *btree, void *score, void **value, rl_btree_node *** nodes, long **positions)
{
	if ((!nodes && positions) || (nodes && !positions)) {
		return RL_INVALID_PARAMETERS;
	}
	rl_btree_node *node = btree->accessor->select(btree, btree->root);
	long i, pos, min, max;
	int cmp = 0;
	for (i = 0; i < btree->height; i++) {
		if (nodes) {
			(*nodes)[i] = node;
		}
		pos = 0;
		min = 0;
		max = node->size - 1;
		while (min <= max) {
			pos = (max - min) / 2 + min;
			cmp = btree->type->cmp(score, node->scores[pos]);
			if (cmp == 0) {
				if (value && node->values) {
					*value = node->values[pos];
				}
				if (nodes) {
					if (positions) {
						(*positions)[i] = pos;
					}
					for (i++; i < btree->height; i++) {
						(*nodes)[i] = NULL;
					}
				}
				return RL_FOUND;
			}
			else if (cmp > 0) {
				if (max == pos) {
					pos++;
					break;
				}
				if (min == pos) {
					min++;
					continue;
				}
				min = pos;
			}
			else {
				if (max == pos) {
					break;
				}
				max = pos;
			}
		}
		if (positions) {
			if (pos == max && btree->type->cmp(score, node->scores[pos]) == 1) {
				pos++;
			}
			(*positions)[i] = pos;
		}
		if (node->children) {
			node = btree->accessor->select(btree, node->children[pos]);
		}
	}
	return RL_NOT_FOUND;
}

int rl_btree_add_element(rl_btree *btree, void *score, void *value)
{
	int retval;
	long *positions = NULL;
	rl_btree_node **nodes = malloc(sizeof(rl_btree_node *) * btree->height);
	if (!nodes) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	positions = malloc(sizeof(long) * btree->height);
	if (!positions) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	void *tmp;
	long i, pos;
	long child = -1;
	retval = rl_btree_find_score(btree, score, NULL, &nodes, &positions);

	if (retval != RL_NOT_FOUND) {
		goto cleanup;
	}
	retval = RL_OK;
	rl_btree_node *node = NULL;
	for (i = btree->height - 1; i >= 0; i--) {
		node = nodes[i];
		if (node->size < btree->max_node_size) {
			memmove_dbg(&node->scores[positions[i] + 1], &node->scores[positions[i]], sizeof(void *) * (node->size - positions[i]), __LINE__);
			if (node->values) {
				memmove_dbg(&node->values[positions[i] + 1], &node->values[positions[i]], sizeof(void *) * (node->size - positions[i]), __LINE__);
			}
			if (node->children) {
				memmove_dbg(&node->children[positions[i] + 2], &node->children[positions[i] + 1], sizeof(long) * (node->size - positions[i]), __LINE__);
			}
			node->scores[positions[i]] = score;
			if (node->values) {
				node->values[positions[i]] = value;
			}
			if (child != -1) {
				if (!node->children) {
					fprintf(stderr, "Adding child, but children is not initialized\n");
				}
				node->children[positions[i] + 1] = child;
			}
			node->size++;
			score = NULL;
			break;
		}
		else {
			pos = positions[i];

			rl_btree_node *right;
			retval = rl_btree_node_create(btree, &right);
			if (retval != RL_OK) {
				goto cleanup;
			}
			if (child != -1) {
				right->children = malloc(sizeof(long) * (btree->max_node_size + 1));
				if (!right->children) {
					retval = RL_OUT_OF_MEMORY;
					goto cleanup;
				}
			}

			if (pos < btree->max_node_size / 2) {
				if (child != -1) {
					memmove_dbg(right->children, &node->children[btree->max_node_size / 2], sizeof(void *) * (btree->max_node_size / 2 + 1), __LINE__);
					memmove_dbg(&node->children[pos + 2], &node->children[pos + 1], sizeof(void *) * (btree->max_node_size / 2 - 1 - pos), __LINE__);
					node->children[pos + 1] = child;

				}
				tmp = node->scores[btree->max_node_size / 2 - 1];
				memmove_dbg(&node->scores[pos + 1], &node->scores[pos], sizeof(void *) * (btree->max_node_size / 2 - 1 - pos), __LINE__);
				node->scores[pos] = score;
				score = tmp;
				memmove_dbg(right->scores, &node->scores[btree->max_node_size / 2], sizeof(void *) * btree->max_node_size / 2, __LINE__);
				if (node->values) {
					tmp = node->values[btree->max_node_size / 2 - 1];
					memmove_dbg(&node->values[pos + 1], &node->values[pos], sizeof(void *) * (btree->max_node_size / 2 - 1 - pos), __LINE__);
					node->values[pos] = value;
					memmove_dbg(right->values, &node->values[btree->max_node_size / 2], sizeof(void *) * btree->max_node_size / 2, __LINE__);
					value = tmp;
				}
			}

			if (pos == btree->max_node_size / 2) {
				if (child != -1) {
					memmove_dbg(&right->children[1], &node->children[btree->max_node_size / 2 + 1], sizeof(void *) * (btree->max_node_size / 2), __LINE__);
					right->children[0] = child;
				}
				memmove_dbg(right->scores, &node->scores[btree->max_node_size / 2], sizeof(void *) * btree->max_node_size / 2, __LINE__);
				if (node->values) {
					memmove_dbg(right->values, &node->values[btree->max_node_size / 2], sizeof(void *) * btree->max_node_size / 2, __LINE__);
				}
			}

			if (pos > btree->max_node_size / 2) {
				if (child != -1) {
					memmove_dbg(right->children, &node->children[btree->max_node_size / 2 + 1], sizeof(void *) * (pos - btree->max_node_size / 2), __LINE__);
					right->children[pos - btree->max_node_size / 2] = child;
					memmove_dbg(&right->children[pos - btree->max_node_size / 2 + 1], &node->children[pos + 1], sizeof(void *) * (btree->max_node_size - pos), __LINE__);
				}
				tmp = node->scores[btree->max_node_size / 2];
				memmove_dbg(right->scores, &node->scores[btree->max_node_size / 2 + 1], sizeof(void *) * (pos - btree->max_node_size / 2 - 1), __LINE__);
				right->scores[pos - btree->max_node_size / 2 - 1] = score;
				memmove_dbg(&right->scores[pos - btree->max_node_size / 2], &node->scores[pos], sizeof(void *) * (btree->max_node_size - pos), __LINE__);
				score = tmp;
				if (node->values) {
					tmp = node->values[btree->max_node_size / 2];
					memmove_dbg(right->values, &node->values[btree->max_node_size / 2 + 1], sizeof(void *) * (pos - btree->max_node_size / 2 - 1), __LINE__);
					right->values[pos - btree->max_node_size / 2 - 1] = value;
					memmove_dbg(&right->values[pos - btree->max_node_size / 2], &node->values[pos], sizeof(void *) * (btree->max_node_size - pos), __LINE__);
					value = tmp;
				}
			}

			node->size = right->size = btree->max_node_size / 2;
			btree->accessor->insert(btree, &child, right);
		}
	}
	if (score) {
		rl_btree_node *old_root = node;
		retval = rl_btree_node_create(btree, &node);
		if (retval != RL_OK) {
			goto cleanup;
		}
		node->size = 1;
		node->scores[0] = score;
		if (node->values) {
			node->values[0] = value;
		}
		node->children = malloc(sizeof(long) * (btree->max_node_size + 1));
		if (!node->children) {
			retval = RL_OUT_OF_MEMORY;
			goto cleanup;
		}
		btree->accessor->update(btree, &node->children[0], old_root);
		node->children[1] = child;
		btree->accessor->insert(btree, &btree->root, node);
		btree->height++;
	}

cleanup:
	free(nodes);
	free(positions);

	return retval;
}

int rl_btree_remove_element(rl_btree *btree, void *score)
{
	int retval;
	long *positions = NULL;
	rl_btree_node **nodes = malloc(sizeof(rl_btree_node *) * btree->height);
	if (!nodes) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	positions = malloc(sizeof(long) * btree->height);
	if (!positions) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	long i, j;
	retval = rl_btree_find_score(btree, score, NULL, &nodes, &positions);

	if (retval != RL_FOUND) {
		goto cleanup;
	}
	retval = RL_OK;

	rl_btree_node *node, *parent_node, *sibling_node, *child_node;
	for (i = btree->height - 1; i >= 0; i--) {
		node = nodes[i];
		if (!node) {
			continue;
		}

		free(node->scores[positions[i]]);
		if (node->children) {
			j = i;
			child_node = node;
			for (; i < btree->height - 1; i++) {
				child_node = btree->accessor->select(btree, child_node->children[positions[i]]);
				nodes[i + 1] = child_node;
				positions[i + 1] = child_node->size;
			}

			if (child_node->children) {
				fprintf(stderr, "last child_node mustn't have children\n");
				retval = 1;
				goto cleanup;
			}

			// only the leaf node loses an element, to replace the deleted one
			child_node->size--;
			node->scores[positions[j]] = child_node->scores[child_node->size];
			if (node->values) {
				node->values[positions[j]] = child_node->values[child_node->size];
			}
			btree->accessor->update(btree, NULL, node);
			btree->accessor->update(btree, NULL, child_node);
			break;
		}
		else {
			memmove_dbg(&node->scores[positions[i]], &node->scores[positions[i] + 1], sizeof(void *) * (node->size - positions[i] - 1), __LINE__);
			if (node->values) {
				memmove_dbg(&node->values[positions[i]], &node->values[positions[i] + 1], sizeof(void *) * (node->size - positions[i]), __LINE__);
			}

			if (--node->size > 0) {
				btree->accessor->update(btree, NULL, node);
			}
			else {
				if (i == 0 && node->children) {
					// we have an empty root, promote the only child if any
					btree->height--;
					btree->accessor->update(btree, &btree->root, btree->accessor->select(btree, node->children[0]));
					btree->accessor->remove(btree, node);
					retval = rl_btree_node_destroy(btree, node);
					if (retval != RL_OK) {
						goto cleanup;
					}
				}
			}
			break;
		}
	}

	for (; i >= 0; i--) {
		node = nodes[i];
		if (i == 0) {
			if (node->size == 0) {
				btree->height--;
				if (node->children) {
					btree->root = node->children[0];
				}
			}
			break;
		}
		if (node->size >= btree->max_node_size / 2 || i == 0) {
			break;
		}
		else {
			parent_node = nodes[i - 1];
			if (parent_node->size == 0) {
				fprintf(stderr, "Empty parent\n");
				retval = 1;
				goto cleanup;
			}
			if (positions[i - 1] > 0) {
				sibling_node = btree->accessor->select(btree, parent_node->children[positions[i - 1] - 1]);
				if (sibling_node->size > btree->max_node_size / 2) {
					memmove_dbg(&node->scores[1], &node->scores[0], sizeof(void *) * (node->size), __LINE__);
					if (node->values) {
						memmove_dbg(&node->values[1], &node->values[0], sizeof(void *) * (node->size), __LINE__);
					}
					if (node->children) {
						memmove_dbg(&node->children[1], &node->children[0], sizeof(long) * (node->size + 1), __LINE__);
						node->children[0] = sibling_node->children[sibling_node->size];
					}
					node->scores[0] = parent_node->scores[positions[i - 1] - 1];
					if (node->values) {
						node->values[0] = parent_node->values[positions[i - 1] - 1];
					}

					parent_node->scores[positions[i - 1] - 1] = sibling_node->scores[sibling_node->size - 1];
					if (parent_node->values) {
						parent_node->values[0] = sibling_node->values[sibling_node->size - 1];
					}

					sibling_node->size--;
					node->size++;
					btree->accessor->update(btree, NULL, sibling_node);
					btree->accessor->update(btree, NULL, node);
					break;
				}
			}
			if (positions[i - 1] < parent_node->size) {
				sibling_node = btree->accessor->select(btree, parent_node->children[positions[i - 1] + 1]);
				if (sibling_node->size > btree->max_node_size / 2) {
					node->scores[node->size] = parent_node->scores[positions[i - 1]];
					if (node->values) {
						node->values[node->size] = parent_node->values[positions[i - 1]];
					}

					parent_node->scores[positions[i - 1]] = sibling_node->scores[0];
					if (parent_node->values) {
						parent_node->values[positions[i - 1]] = sibling_node->values[0];
					}

					memmove_dbg(&sibling_node->scores[0], &sibling_node->scores[1], sizeof(void *) * (sibling_node->size - 1), __LINE__);
					if (node->values) {
						memmove_dbg(&sibling_node->values[0], &sibling_node->values[1], sizeof(void *) * (sibling_node->size - 1), __LINE__);
					}
					if (node->children) {
						node->children[node->size + 1] = sibling_node->children[0];
						memmove_dbg(&sibling_node->children[0], &sibling_node->children[1], sizeof(long) * (sibling_node->size), __LINE__);
					}

					sibling_node->size--;
					node->size++;
					btree->accessor->update(btree, NULL, sibling_node);
					btree->accessor->update(btree, NULL, node);
					break;
				}
			}

			// not taking from either slibing, need to merge with either
			// if either of them exists, they have the minimum number of elements
			if (positions[i - 1] > 0) {
				sibling_node = btree->accessor->select(btree, parent_node->children[positions[i - 1] - 1]);
				sibling_node->scores[sibling_node->size] = parent_node->scores[positions[i - 1] - 1];
				if (parent_node->values) {
					sibling_node->values[sibling_node->size] = parent_node->values[positions[i - 1] - 1];
				}
				memmove_dbg(&sibling_node->scores[sibling_node->size + 1], &node->scores[0], sizeof(void *) * (node->size), __LINE__);
				if (sibling_node->values) {
					memmove_dbg(&sibling_node->values[sibling_node->size + 1], &node->values[0], sizeof(void *) * (node->size), __LINE__);
				}
				if (sibling_node->children) {
					memmove_dbg(&sibling_node->children[sibling_node->size + 1], &node->children[0], sizeof(void *) * (node->size + 1), __LINE__);
				}

				if (positions[i - 1] < parent_node->size) {
					memmove_dbg(&parent_node->scores[positions[i - 1] - 1], &parent_node->scores[positions[i - 1]], sizeof(void *) * (parent_node->size - positions[i - 1]), __LINE__);
					if (parent_node->values) {
						memmove_dbg(&parent_node->values[positions[i - 1] - 1], &parent_node->values[positions[i - 1]], sizeof(void *) * (parent_node->size - positions[i - 1]), __LINE__);
					}
					memmove_dbg(&parent_node->children[positions[i - 1]], &parent_node->children[positions[i - 1] + 1], sizeof(void *) * (parent_node->size - positions[i - 1]), __LINE__);
				}
				parent_node->size--;
				sibling_node->size += 1 + node->size;
				btree->accessor->update(btree, NULL, sibling_node);
				btree->accessor->update(btree, NULL, parent_node);
				btree->accessor->remove(btree, node);
				free(node->scores);
				node->scores = NULL;
				retval = rl_btree_node_destroy(btree, node);
				if (retval != RL_OK) {
					goto cleanup;
				}
				continue;
			}
			if (positions[i - 1] < parent_node->size) {
				sibling_node = btree->accessor->select(btree, parent_node->children[positions[i - 1] + 1]);
				node->scores[node->size] = parent_node->scores[positions[i - 1]];
				if (parent_node->values) {
					node->values[node->size] = parent_node->values[positions[i - 1]];
				}
				memmove_dbg(&node->scores[node->size + 1], &sibling_node->scores[0], sizeof(void *) * (sibling_node->size), __LINE__);
				if (node->values) {
					memmove_dbg(&node->values[node->size + 1], &sibling_node->values[0], sizeof(void *) * (sibling_node->size), __LINE__);
				}
				if (node->children) {
					memmove_dbg(&node->children[node->size + 1], &sibling_node->children[0], sizeof(void *) * (sibling_node->size + 1), __LINE__);
				}


				memmove_dbg(&parent_node->scores[positions[i - 1]], &parent_node->scores[positions[i - 1] + 1], sizeof(void *) * (parent_node->size - positions[i - 1] - 1), __LINE__);
				if (parent_node->values) {
					memmove_dbg(&parent_node->values[positions[i - 1]], &parent_node->values[positions[i - 1] + 1], sizeof(void *) * (parent_node->size - positions[i - 1] - 1), __LINE__);
				}
				memmove_dbg(&parent_node->children[positions[i - 1] + 1], &parent_node->children[positions[i - 1] + 2], sizeof(void *) * (parent_node->size - positions[i - 1] - 1), __LINE__);

				parent_node->size--;
				node->size += 1 + sibling_node->size;
				btree->accessor->update(btree, NULL, node);
				btree->accessor->update(btree, NULL, parent_node);
				// freeing manually scores before calling destroy to avoid deleting scores that were handed over to `node`
				free(sibling_node->scores);
				sibling_node->scores = NULL;
				btree->accessor->remove(btree, sibling_node);
				retval = rl_btree_node_destroy(btree, sibling_node);
				if (retval != RL_OK) {
					goto cleanup;
				}
				continue;
			}

			// this shouldn't happen
			fprintf(stderr, "No sibling to borrow or merge\n");
			retval = RL_INVALID_STATE;
			goto cleanup;
		}
	}

cleanup:
	free(nodes);
	free(positions);

	return retval;
}

int rl_btree_node_is_balanced(rl_btree *btree, rl_btree_node *node, int is_root)
{
	if (!is_root && node->size < btree->max_node_size / 2) {
		return RL_OK;
	}

	int i, retval;
	for (i = 0; i < node->size + 1; i++) {
		if (node->children) {
			retval = rl_btree_node_is_balanced(btree, btree->accessor->select(btree, node->children[i]), 0);
			if (retval != RL_OK) {
				break;
			}
		}
	}
	return retval;
}

int rl_btree_is_balanced(rl_btree *btree)
{
	void **scores = NULL;
	int retval = rl_btree_node_is_balanced(btree, btree->accessor->select(btree, btree->root), 1);
	if (retval != RL_OK) {
		goto cleanup;
	}

	long size = (long)pow(btree->max_node_size + 1, btree->height + 1);
	scores = malloc(sizeof(void *) * size);
	if (!scores) {
		retval = RL_OUT_OF_MEMORY;
		goto cleanup;
	}
	size = 0;
	rl_flatten_btree(btree, &scores, &size);
	long i, j;
	for (i = 0; i < size; i++) {
		for (j = i + 1; j < size; j++) {
			if (btree->type->cmp(scores[i], scores[j]) == 0) {
				retval = RL_INVALID_STATE;
				goto cleanup;
			}
		}
	}
cleanup:
	free(scores);
	return retval;
}

int rl_print_btree_node(rl_btree *btree, rl_btree_node *node, long level)
{
	int retval = RL_OK;
	long i, j;
	if (node->children) {
		retval = rl_print_btree_node(btree, btree->accessor->select(btree, node->children[0]), level + 1);
		if (retval != RL_OK) {
			goto cleanup;
		}
	}
	char *score;
	int size;
	for (i = 0; i < node->size; i++) {
		for (j = 0; j < level; j++) {
			putchar('=');
		}
		btree->type->formatter(node->scores[i], &score, &size);
		fwrite(score, sizeof(char), size, stdout);
		free(score);
		printf("\n");
		if (node->values) {
			for (j = 0; j < level; j++) {
				putchar('*');
			}
			printf("%p\n", node->values[i]);
		}
		printf("\n");
		if (node->children) {
			retval = rl_print_btree_node(btree, btree->accessor->select(btree, node->children[i + 1]), level + 1);
			if (retval != RL_OK) {
				goto cleanup;
			}
		}
	}
cleanup:
	return retval;
}

int rl_print_btree(rl_btree *btree)
{
	printf("-------\n");
	int retval = rl_print_btree_node(btree, btree->accessor->select(btree, btree->root), 1);
	printf("-------\n");
	return retval;
}

int rl_flatten_btree_node(rl_btree *btree, rl_btree_node *node, void *** scores, long *size)
{
	int retval = RL_OK;
	long i;
	if (node->children) {
		retval = rl_flatten_btree_node(btree, btree->accessor->select(btree, node->children[0]), scores, size);
		if (retval != RL_OK) {
			goto cleanup;
		}
	}
	for (i = 0; i < node->size; i++) {
		(*scores)[*size] = node->scores[i];
		(*size)++;
		if (node->children) {
			retval = rl_flatten_btree_node(btree, btree->accessor->select(btree, node->children[i + 1]), scores, size);
			if (retval != RL_OK) {
				goto cleanup;
			}
		}
	}
cleanup:
	return retval;
}

int rl_flatten_btree(rl_btree *btree, void *** scores, long *size)
{
	return rl_flatten_btree_node(btree, btree->accessor->select(btree, btree->root), scores, size);
}
