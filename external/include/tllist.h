#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <assert.h>

#define TLL_PASTE2( a, b) a##b
#define TLL_PASTE( a, b) TLL_PASTE2( a, b)

/* Utility macro to generate a list element struct with a unique struct tag */
#define TLL_UNIQUE_INNER_STRUCT(TYPE, ID)      \
    struct TLL_PASTE(__tllist_ , ID) {         \
        TYPE item;                             \
        struct TLL_PASTE(__tllist_, ID) *prev; \
        struct TLL_PASTE(__tllist_, ID) *next; \
    } *head, *tail;

/*
 * Defines a new typed-list type, or directly instantiate a typed-list variable
 *
 * Example a, declare a variable (list of integers):
 *  tll(int) my_list;
 *
 * Example b, declare a type, and then use the type:
 *   tll(int, my_list_type);
 *   struct my_list_type my_list;
 */
#define tll(TYPE)                                                       \
    struct {                                                            \
        TLL_UNIQUE_INNER_STRUCT(TYPE, __COUNTER__)                      \
        size_t length;                                                  \
    }

/* Initializer: tll(int) my_list = tll_init(); */
#define tll_init() {.head = NULL, .tail = NULL, .length = 0}

/* Length/size of list: printf("size: %zu\n", tll_length(my_list)); */
#define tll_length(list) (list).length

/* Adds a new item to the back of the list */
#define tll_push_back(list, new_item)                   \
    do {                                                \
        tll_insert_after(list, (list).tail, new_item);  \
        if ((list).head == NULL)                        \
            (list).head = (list).tail;                  \
    } while (0)

/* Adds a new item to the front of the list */
#define tll_push_front(list, new_item) \
    do {                                                \
        tll_insert_before(list, (list).head, new_item); \
        if ((list).tail == NULL)                        \
            (list).tail = (list).head;                  \
    } while (0)

/*
 * Iterates the list. <it> is an iterator pointer. You can access the
 * list item with ->item:
 *
 *   tll(int) my_list = vinit();
 *   tll_push_back(my_list, 5);
 *
 *   tll_foreach(my_list i) {
 *       printf("%d\n", i->item);
 *   }
*/
#define tll_foreach(list, it)                                           \
    for (__typeof__(*(list).head) *it = (list).head,                    \
             *it_next = it != NULL ? it->next : NULL;                   \
         it != NULL;                                                    \
         it = it_next,                                                  \
             it_next = it_next != NULL ? it_next->next : NULL)

/* Same as tll_foreach(), but iterates backwards */
#define tll_rforeach(list, it)                                          \
    for (__typeof__(*(list).tail) *it = (list).tail,                    \
             *it_prev = it != NULL ? it->prev : NULL;                   \
         it != NULL;                                                    \
         it = it_prev,                                                  \
             it_prev = it_prev != NULL ? it_prev->prev : NULL)

/*
 * Inserts a new item after <it>, which is an iterator. I.e. you can
 * only call this from inside a tll_foreach() or tll_rforeach() loop.
 */
#define tll_insert_after(list, it, new_item) \
    do {                                                    \
        __typeof__((list).head) __e = malloc(sizeof(*__e)); \
        __e->item = (new_item);                             \
        __e->prev = (it);                                   \
        __e->next = (it) != NULL ? (it)->next : NULL;       \
        if ((it) != NULL) {                                 \
            if ((it)->next != NULL)                         \
                (it)->next->prev = __e;                     \
            (it)->next = __e;                               \
        }                                                   \
        if ((it) == (list).tail)                            \
            (list).tail = __e;                              \
        (list).length++;                                    \
    } while (0)

/*
 * Inserts a new item before <it>, which is an iterator. I.e. you can
 * only call this from inside a tll_foreach() or tll_rforeach() loop.
 */
#define tll_insert_before(list, it, new_item)   \
    do {                                                    \
        __typeof__((list).head) __e = malloc(sizeof(*__e)); \
        __e->item = (new_item);                             \
        __e->prev = (it) != NULL ? (it)->prev : NULL;       \
        __e->next = (it);                                   \
        if ((it) != NULL) {                                 \
            if ((it)->prev != NULL)                         \
                (it)->prev->next = __e;                     \
            (it)->prev = __e;                               \
        }                                                   \
        if ((it) == (list).head)                            \
            (list).head = __e;                              \
        (list).length++;                                    \
    } while (0)

/*
 * Removes an entry from the list. <it> is an iterator. I.e. you can
 * only call this from inside a tll_foreach() or tll_rforeach() loop.
 */
#define tll_remove(list, it)                              \
    do {                                                  \
        assert((list).length > 0);                        \
        __typeof__((list).head) __prev = it->prev;        \
        __typeof__((list).head) __next = it->next;        \
        if (__prev != NULL)                               \
            __prev->next = __next;                        \
        else                                              \
            (list).head = __next;                         \
        if (__next != NULL)                               \
            __next->prev = __prev;                        \
        else                                              \
            (list).tail = __prev;                         \
        free(it);                                         \
        (list).length--;                                  \
    } while (0)

/* Same as tll_remove(), but calls free_callback(it->item) */
#define tll_remove_and_free(list, it, free_callback)            \
    do {                                                        \
        free_callback((it)->item);                              \
        tll_remove((list), (it));                               \
    } while (0)

#define tll_front(list) (list).head->item
#define tll_back(list) (list).tail->item

/*
 * Removes the first element from the list, and returns it (note:
 * returns the *actual* item, not an iterator.
 */
#define tll_pop_front(list) __extension__                  \
    ({                                                     \
        __typeof__((list).head) it = (list).head;          \
        __typeof__((list).head->item) __ret = it->item;    \
        tll_remove((list), it);                            \
        __ret;                                             \
    })

/* Same as tll_pop_front(), but returns/removes the *last* element */
#define tll_pop_back(list) __extension__                                \
    ({                                                                  \
        __typeof__((list).tail) it = (list).tail;                       \
        __typeof__((list).tail->item) __ret = it->item;                 \
        tll_remove((list), it);                                         \
        __ret;                                                          \
    })

/* Frees the list. This call is *not* needed if the list is already empty. */
#define tll_free(list)                          \
    do {                                        \
        tll_foreach(list, __it)                 \
            free(__it);                         \
        (list).length = 0;                      \
        (list).head = (list).tail = NULL;       \
    } while (0)

/* Same as tll_free(), but also calls free_callback(item) for every item */
#define tll_free_and_free(list, free_callback)              \
    do {                                                    \
        tll_foreach(list, __it) {                           \
            free_callback(__it->item);                      \
            free(__it);                                     \
        }                                                   \
        (list).length = 0;                                  \
        (list).head = (list).tail = NULL;                   \
    } while (0)

#define tll_sort(list, cmp)                                                 \
    do {                                                                    \
	__typeof((list).head) __p;                                          \
	__typeof((list).head) __q;                                          \
	__typeof((list).head) __e;                                          \
	__typeof((list).head) __t;                                          \
        int __insize, __nmerges, __p_size, __q_size;                        \
	if ((list).head == NULL)                                            \
	    break;                                                          \
        __insize = 1;                                                       \
        while (1) {                                                         \
		__p = (list).head;                                          \
		(list).head = NULL;                                         \
                __t = NULL;                                                 \
		__nmerges = 0;                                              \
		while (__p != NULL) {                                       \
	            __nmerges++;                                            \
	            __q = __p;                                              \
		    __p_size = 0;                                           \
		    for (int _i = 0; _i < __insize; _i++) {                 \
			    __p_size++;                                     \
			    __q = __q->next;                                \
			    if (__q == NULL)                                \
			        break;                                      \
		    }                                                       \
	            __q_size = __insize;                                    \
		    while (__p_size > 0 || (__q_size > 0 && __q != NULL)) { \
			    if (__p_size == 0) {                            \
				    __e = __q;                              \
				    __q = __q->next;                        \
				    __q_size--;                             \
			    } else if (__q_size == 0 || __q == NULL) {      \
				    __e = __p;                              \
				    __p = __p->next;                        \
				    __p_size--;                             \
			    } else if (cmp(__p->item, __q->item) <= 0) {    \
				    __e = __p;                              \
				    __p = __p->next;                        \
				    __p_size--;                             \
			    } else {                                        \
				    __e = __q;                              \
				    __q = __q->next;                        \
				    __q_size--;                             \
			    }                                               \
			    if (__t != NULL) {                              \
				    __t->next = __e;                        \
			    } else {                                        \
				    (list).head = __e;                      \
			    }                                               \
			    __e->prev = __t;                                \
			    __t = __e;                                      \
		    }                                                       \
	            __p = __q;                                              \
		}                                                           \
		(list).tail = __t;                                          \
		__t->next = NULL;                                           \
		__insize *= 2;                                              \
		if (__nmerges <= 1)                                         \
			break;                                              \
	}                                                                   \
    } while (0)
