#ifndef LIST_H
#define LIST_H


/*
 * Simple doubly linked list implementation.  简单的双向链表实现
 *
 * Some of the internal functions ("__xxx") are useful when
 * manipulating whole lists rather than single entries, as
 * sometimes we already know the next/prev entries and we can
 * generate better code by using them directly rather than
 * using the generic single-entry routines.
 * 有一些内部函数("__xxx")是非常有用的当你操纵整个链表而不是
 * 单个条目的时候。因为有的时候我们已经知道下一个条目并且我们可以
 * 通过直接使用他们而不是使用普遍的单条目程序来生成更好的代码。
 */


struct list_head {
	struct list_head *next, *prev;  //双向链表，两个指针，一个指向上一个list_head，另一个指向下一个list_head
};




/**
 * 宏定义，用于初始化双向链表的两个节点
 * 由于宏相当于替代作用，则在第二个宏中，替代完成之后，就变成了：
 * struct list_head name = {&(name),&(name)};
 * 也就相当于一个构造函数进行初始化工作，转换化之后就变成：
 * 
 * struct list_head head; 
 * head.next = &head;
 * head.prev = &head;
 * 构造一个指向自身的空循环链表
 * 
 */
#define LIST_HEAD_INIT(name) { &(name), &(name) }   //其中&(name)中的括号是不能去掉的，如果去掉之后 LIST_HEAD_INIT(A+B) 就成了 {&A+B,&A+B}了


#define LIST_HEAD(name)	struct list_head name = LIST_HEAD_INIT(name)    //在宏定义中，使用"\"当作换行符



//以list为头节点初始化循环链表，使头节点和尾节点都指向自己
static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}


/*
 * Insert a new entry between two known consecutive entries.
 * 在已知的两个连续的条目中面插入一个条目
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 * 这个仅仅用于我们已经知道上一个/下一个条目的情况下对链表的内部
 * 操作
 *
 */
static inline void __list_add(struct list_head *new_node,  
			      struct list_head *prev,
			      struct list_head *next)   //(新插入的节点，上一个节点，下一个节点)
{
	next->prev = new_node;                              //
	new_node->next = next;                              // A-B-C   //B为prev,C为next,则在B，C之间插入new
	new_node->prev = prev;			       // 相应的C的上一个为new;new的下一个为C
	prev->next = new_node;			       // new的上一个为prev;prev的下一个为new
}


/**
 * list_add - add a new entry   添加一个新的条目(体现比较好的封装性)
 * @new: new entry to be added  被添加的新的条目
 * @head: list head to add it after  新条目被添加到head之后
 *
 * Insert a new entry after the specified head.   在某个节点之后新添加一个条目
 * This is good for implementing stacks.          这个适用于实现栈     
 */
static inline void list_add_head(struct list_head *new_node, struct list_head *head)
{
	__list_add(new_node, head, head->next);   //在这，head相当于__list_add的上一个，head->next相当于__list_add中的下一个
}


/**
 * list_add_tail - add a new entry  添加一个新的条目
 * @new: new entry to be added      被添加的新的条目
 * @head: list head to add it before 在head节点之前添加新节点
 *
 * Insert a new entry before the specified head.  在某个节点之前新添加一个条目
 * This is useful for implementing queues.        这个适用于实现队列
 */
static inline void list_add_tail(struct list_head *new_node, struct list_head *head)
{
	__list_add(new_node, head->prev, head);   //在这，head->prev相当于__list_add的上一个，head相当于__list_add中的下一个
}


/*
 * Delete a list entry by making the prev/next entries
 * point to each other.        通过操纵某一个节点的上一个/下一个条目来删除一个节点
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!   这个仅仅就是知道上一个/下一个条目之后的内部操作
 */
static inline void __list_del(struct list_head * prev, struct list_head * next)
{
	next->prev = prev;              
	prev->next = next;


	/**
 	 * A-B-C
	 * 想要删除节点B，而A上B的上一个节点，即为函数中的prev;
	 * C为B的下一个节点，即为函数中的next。
	 * 删除B，即为A的next指向C
	 * C的prev指向A
	 * 即prev->next = next;
	 *   next->prev = prev;  即可
	 */
}


/**
 * list_del - deletes entry from list.        从一个链表中删除一个条目
 * @entry: the element to delete from the list.   要从链表中被删除的条目
 * Note: list_empty on entry does not return true after this, the entry is
 * in an undefined state.
 *
 * 注意：在执行该操作之后，在被删除的这个条目上使用list_empty函数不会返回true，
 * 这个条目现在处于一个未被定义的状态
 *	
 */
static inline void list_del(struct list_head *entry)
{
	__list_del(entry->prev, entry->next);
   
         //防止被删除的条目乱指引起错误，进行这样的处理
//	entry->next = LIST_POISON1;
//	entry->prev = LIST_POISON2;


	/*
 	 * These are non-NULL pointers that will result in page faults
 	 * under normal circumstances, used to verify that nobody uses
 	 * non-initialized list entries.
	 *
	 * 这是两个是在正常环境下会导致页错误的非空指针，
	 * 这两个被用来确定没有人使用未初始化的链表条目
 	 */
	// #define LIST_POISON1  ((void *) 0x00100100)
	// #define LIST_POISON2  ((void *) 0x00200200)
}


/**
 * list_replace - replace old entry by new one
 * list_replace - 使用新的条目替换旧的条目
 * @old : the element to be replaced   被替换的元素
 * @new : the new element to insert    替换的新元素
 * Note: if 'old' was empty, it will be overwritten. 
 * 注意：如果旧元素是空的，他将被重写，相当于覆盖
 */
static inline void list_replace(struct list_head *old,
				struct list_head *new_node)
{
	new_node->next = old->next;       //A-B-C
	new_node->next->prev = new_node;       //删除B节点，使用D来替代的话
	new_node->prev = old->prev;       //D的下一个等于B的下一个，D的上一个等于B的上一个
	new_node->prev->next = new_node;       //C的上一个等于D，A的下一个等于D ; C相当于B的下一个，即B的下一个的上一个就是B
}




/**
 * 该函数的作用是，将被替代的元素old的next/prev指向自己
 * 以免其指针乱指，导致程序换乱
 */
static inline void list_replace_init(struct list_head *old,
					struct list_head *new_node)
{
	list_replace(old, new_node);   //旧的替换新的
	INIT_LIST_HEAD(old);      //将old的指针指向自己以免混乱
}

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * list_del_init - 从链表中删除元素并且重新初始化他
 * @entry: the element to delete from the list. 从链表中要被删除的元素
 */
static inline void list_del_init(struct list_head *entry)
{
	__list_del(entry->prev, entry->next); 
	INIT_LIST_HEAD(entry);   //重新初始化一个链表
}


/**
 * list_move - delete from one list and add as another's head
 * list_move - 从一个链表中删除一个元素并且该元素作为另一个元素的头节点
 * @list: the entry to move   要被移动的元素
 * @head: the head that will precede our entry  在我们元素之前的头元素  
 */
static inline void list_move(struct list_head *list, struct list_head *head)
{
        __list_del(list->prev, list->next);  //删除list元素
        list_add_head(list, head);  //__list_add(list,head,head->next); //相当于在list元素之后添加一个元素
}


/**
 * list_is_last - tests whether @list is the last entry in list @head
 * list_is_last - 测试list这个节点是否是链表的最后一个条目
 * @list: the entry to test   要被测试的节点
 * @head: the head of the list  链表的头节点
 */
static inline int list_is_last(const struct list_head *list,
				const struct list_head *head)
{
	return list->next == head;  //双向循环链表，最后一个节点指向第一个节点
}


/**
 * list_empty - tests whether a list is empty
 * list_empty - 测试一个链表是否为空
 * @head: the list to test.  要被测试的链表
 */
static inline int list_empty(const struct list_head *head)
{
	return head->next == head;   //如果一个双向循环链表为空的话，则他的头节点是指向自己的
}


/**
 * list_empty_careful - tests whether a list is empty and not being modified
 * list_empty_careful - 测试一个链表是否为空，并且这个链表没有正在被修改
 * @head: the list to test  要被prefetch测试的链表
 *
 * Description:
 * tests whether a list is empty _and_ checks that no other CPU might be
 * in the process of modifying either member (next or prev)
 * 描述：
 * 测试一个链表是否为空并且检查没有其他CPU正在修改链表中的任何一个元素
 *
 * NOTE: using list_empty_careful() without synchronization
 * can only be safe if the only activity that can happen
 * to the list entry is list_del_init(). Eg. it cannot be used
 * if another CPU could re-list_add() it.
 *
 * 注意：使用list_empty_careful()prefetch函数而不使用同步的话只有一种情况下是
 * 安全的，也就是只有唯一一个活动对链表元素使用list_del_init().
 * 如果另一个CPU可能使用re_list_add()函数的话，他就不能使用了
 */
static inline int list_empty_careful(const struct list_head *head)
{
	struct list_head *next = head->next;   //首先获取头节点指向的下一个节点
	
	/**
	 * (next == head) && (next == head->prev)有四种可能的情况
	 *       0                   0           -> 链表不为空，则直接返回0
	 *       0                   1           -> 链表不为空，则直接返回0
     *
	 *       1                   0           -> 链表为空，但是head的上一个和head的下一个
	 *                                          不相等，则表示有CPU正在修改链表,返回0
	 *       1                   1           -> 链表为空，head的上一个和head的下一个相等，
 	 *	                                    没有CPU正在修改链表，则返回1
	 */
	return (next == head) && (next == head->prev);  
}


/**
 * 在一个链表上的head节点之后将list链表添加进去
 * 第一个链表：A-B-C;第二个链表list：Q-W-E
 * 在第一个链表的B节点之后将第二个链表list添加进去
 * 那么B的下一个是Q，Q的上一个节点是B
 * C的上一个节点是E，E的下一个节点是C
 * A-B-Q-W-E-C
 */
static inline void __list_splice(struct list_head *list,
				 struct list_head *head)
{
	struct list_head *first = list->next; //获取链表的第一个节点，头节点舍去
	struct list_head *last = list->prev;  //获取链表的最后一个节点
	struct list_head *at = head->next;    //获取head节点的下一个节点
// prefetch
	first->prev = head;    //就相当与注释中的内容了
	head->next = first;


	last->next = at;
	at->prev = last;
}


/**
 * list_splice - join two lists   连接两个链表
 * @list: the new list to add.    要添加进去的链表
 * @head: the place to add it in the first list.  在head节点之后将list链表添加进去
 */
static inline void list_splice(struct list_head *list, struct list_head *head)
{
	if (!list_empty(list))   //首先得判断要添加的链表非空，要是空的话，添加进去就会出现问题
		__list_splice(list, head);
}


/**
 * list_splice_init - join two lists and reinitialise the emptied list.
 * list_splice_init - 连接两个链表并且重新初始化被清空的链表
 * @list: the new list to add.   要添加进入另一个链表中的链表
 * @head: the place to add it in the first list.   在head节点之后将list链表添加进去
 *
 * The list at @list is reinitialised  list链表在list节点被重新初始化
 */
static inline void list_splice_init(struct list_head *list,
				    struct list_head *head)
{
	if (!list_empty(list)) {
		__list_splice(list, head);
		INIT_LIST_HEAD(list);   //以list链表为头节点初始化链表
	}
}

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)
#endif

#define container_of(ptr, type, member) ({ \
const typeof(((type *)0)->member) *__mptr = (ptr); \
(type *)((char *)__mptr - offsetof(type, member));})


/**
 * list_entry - get the struct for this entry   
 * list_entry - 获得这个元素的结构
 * @ptr:	the &struct list_head pointer.  结构在链表中的指针
 * @type:	the type of the struct this is embedded in.  被嵌入的结构的类型
 * @member:	the name of the list_struct within the struct.  //在结构内部链表结构的名称
 */
#define list_entry(ptr, type, member) \
	container_of(ptr, type, member)   //container_of()函数是一个非常重要的函数，后面接着学习
/**
 * 下面看一下container_of()函数
 * 首先我们需要看一下宏定义 offsetof(TYPE, MEMBER)
 * #define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
 *
 * offsetof(TYPE, MEMBER)的定义相当于 ((size_t) &((TYPE *)0)->MEMBER)
 * size_t : unsigned int
 * 由于结构是以内存空间首地址0作为其起始地址，则将0强制转换为指定类型的结构指针，然后再获取
 * member后的地址即为成员member在其所在结构体中的偏移量
 *
 * container_of - cast a member of a structure out to the containing structure
 * container_of - 根据结构中某一个成员变量的地址获取整个结构的地址
 * @ptr:	the pointer to the member.   成员变量的指针
 * @type:	the type of the container struct this is embedded in.  成员变量所在的结构的类型
 * @member:	the name of the member within the struct.  在结构中的成员变量的名称
 *
 * #define container_of(ptr, type, member) ({			\
 *       const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
 *       (type *)( (char *)__mptr - offsetof(type,member) );})
 *
 * (type *)0 -- 将0强制转化为制定类型的指针
 * ((type *)0)->member -- 获取成员变量名称为member的成员变量
 * typeof( ((type *)0)->member ) -- 获取成员变量的类型
 * 将成员变量的地址赋给_mptr
 * 将成员变量的地址 减去 成员变量在其所在结构体中的偏移量 即为其所在结构体的初始地址
 * 最后在(type *)的作用下获取type *类型的结构体的地址
 * 
 * 举例：
 * struct demo_struct{
 *	type1 member1;
 *  type2 member2;
 *	type3 member3;   //已知成员变量
 *  type4 member4;
 * };
 *
 *	已知根据某一个方法获得了 type3 *memp;
 *	则struct demo_struct *demop = container_of(memp,struct demo_struct,member3);
 *  将container_of函数展开之后：
 *  struct demo_demop = ({	\
 *		 const typeof( ((struct demo_struct *)0)->member3 ) *__mptr = (memp);    \ 
 *        (struct demo_struct *)( (char *)__mptr - offsetof(struct demo_struct, member3) );})
 *
 *
 * demo
 * +-----------+ 0XA000
 * |  member1  | 
 * +-----------+ 0XA004
 * |  member2  |
 * +-----------+ 0XA010
 * |  member3  |        //已知变量
 * +-----------+ 0XA018
 * |  member4  |
 * +-----------+
 *
 * 通过offsetof(struct demo_struct, member3)之后，
 * 获得member3在demo中的偏移量，即 0X10
 * 
 * (struct demo_struct *)( (char *)__mptr = 0XA010
 * 
 * 则两个地址相减，即可获取结构的初始地址
 *
 */
 
/**
 * list_next_entry - get the next element in list
 * @pos:    the type * to cursor
 * @member: the name of the list_head within the struct.
 */
#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)

/**
 * list_prev_entry - get the prev element in list
 * @pos:    the type * to cursor
 * @member: the name of the list_head within the struct.
 */
#define list_prev_entry(pos, member) \
    list_entry((pos)->member.prev, typeof(*(pos)), member)

/**
 * list_for_each - iterate over a list
 * list-for_each - 遍历整个链表
 * @pos:	the &struct list_head to use as a loop cursor. 链表中的结构地址被用作一个循环的游标
 * @head:	the head for your list.     你的链表的头节点
 */
#define list_for_each(pos, head) \
	for (pos = (head)->next; prefetch(pos->next), pos != (head); \
        	pos = pos->next)   //使用该宏之后就可以使用pos对链表中的元素进行处理了


/**
 * __list_for_each - iterate over a list
 * list-for_each - 遍历整个链表
 * @pos:	the &struct list_head to use as a loop cursor. 链表中的结构地址被用作一个循环的游标
 * @head:	the head for your list.  你的链表的头节点
 *
 * This variant differs from list_for_each() in that it's the
 * simplest possible list iteration code, no prefetching is done.
 * Use this for code that knows the list to be very short (empty
 * or 1 entry) most of the time.
 *
 * 这个函数和list_for_each()函数的不同之处就是，这个是最简单的
 * 链表遍历器代码。没有获取元素这一步。
 * 使用这个函数需要知道链表在大多数情况下是非常短的（空或者是只有一个元素）
 *
 */
#define __list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); pos = pos->next)


/**
 * list_for_each_prev - iterate over a list backwards
 * list_for_each_prev - 向后遍历整个链表 
 * @pos:	the &struct list_head to use as a loop cursor. 链表中的结构地址被用作一个循环的游标
 * @head:	the head for your list. 你的链表的头节点
 */
#define list_for_each_prev(pos, head) \
	for (pos = (head)->prev; prefetch(pos->prev), pos != (head); \
        	pos = pos->prev)


/**
 * list_for_each_safe - iterate over a list safe against removal of list entry
 * list_for_each_safe - 安全的遍历整个链表，在遍历过程中不能移除链表元素
 * @pos:	the &struct list_head to use as a loop cursor.  链表中的结构地址被用作一个循环的游标
 * @n:		another &struct list_head to use as temporary storage 连一个节点指针被用作临时存储
 * @head:	the head for your list.  你的链表的头节点
 */
#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)


/**
 * list_for_each_entry - iterate over list of given type  遍历给定类型的链表
 * @pos:	the type * to use as a loop cursor. 类型*被用作循环游标
 * @head:	the head for your list.  你链表的头节点
 * @member:	the name of the list_struct within the struct. 在结构中节点的名称
 */
#define list_for_each_entry(pos, head, member)				\
	for (pos = list_entry((head)->next, typeof(*pos), member);	\
	     prefetch(pos->member.next), &pos->member != (head); 	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))


/**
 * list_for_each_entry_reverse - iterate backwards over list of given type. 向后遍历给定类型的链表
 * @pos:	the type * to use as a loop cursor.  类型*被用作循环游标
 * @head:	the head for your list.    你链表的头节点
 * @member:	the name of the list_struct within the struct. 在结构中节点的名称
 */
#define list_for_each_entry_reverse(pos, head, member)			\
	for (pos = list_entry((head)->prev, typeof(*pos), member);	\
	     prefetch(pos->member.prev), &pos->member != (head); 	\
	     pos = list_entry(pos->member.prev, typeof(*pos), member))


/**
 * list_prepare_entry - prepare a pos entry for use in list_for_each_entry_continue
 * list_prepare_entry - 准备一个指针条目来在list_for_each_entry_continue中使用
 * @pos:	the type * to use as a start point 类型*被用作循环游标
 * @head:	the head of the list 你链表的头节点
 * @member:	the name of the list_struct within the struct. 在结构中节点的名称
 *
 * Prepares a pos entry for use as a start point in list_for_each_entry_continue.
 * 准备的指针条目作为函数list_for_each_entry_continue的开始指针
 */
#define list_prepare_entry(pos, head, member) \
	((pos) ? : list_entry(head, typeof(*pos), member))


/**
 * list_for_each_entry_continue - continue iteration over list of given type  对给定类型的链表继续遍历
 * @pos:	the type * to use as a loop cursor.   类型*被用作循环游标
 * @head:	the head for your list. 你链表的头节点
 * @member:	the name of the list_struct within the struct. 在结构中节点的名称
 *
 * Continue to iterate over list of given type, continuing after
 * the current position.
 * 对给定类型的链表继续遍历,在当前节点之后继续遍历
 * 
 */
#define list_for_each_entry_continue(pos, head, member) 		\
	for (pos = list_entry(pos->member.next, typeof(*pos), member);	\
	     prefetch(pos->member.next), &pos->member != (head);	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))


/**
 * list_for_each_entry_from - iterate over list of given type from the current point 
 * list_for_each_entry_from - 从当前指针开始遍历给定类型的链表
 * @pos:	the type * to use as a loop cursor.  类型*被用作循环游标
 * @head:	the head for your list.  你链表的头节点
 * @member:	the name of the list_struct within the struct.  在结构中节点的名称
 *
 * Iterate over list of given type, continuing from current position.
 * 对给定类型的链表继续遍历,从当前位置继续遍历
 */
#define list_for_each_entry_from(pos, head, member) 			\
	for (; prefetch(pos->member.next), &pos->member != (head);	\
	     pos = list_entry(pos->member.next, typeof(*pos), member))


/**
 * list_for_each_entry_safe - iterate over list of given type safe against removal of list entry
 * list_for_each_entry_safe - 安全的遍历整个链表，组织移除链表中的元素
 * @pos:	the type * to use as a loop cursor. 类型*被用作循环游标
 * @n:		another type * to use as temporary storage  临时存储
 * @head:	the head for your list. 你链表的头节点
 * @member:	the name of the list_struct within the struct. 在结构中节点的名称
 */
#define list_for_each_entry_safe(pos, n, head, member)			\
	for (pos = list_entry((head)->next, typeof(*pos), member),	\
		n = list_entry(pos->member.next, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))


/**
 * list_for_each_entry_safe_continue
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 *
 * Iterate over list of given type, continuing after current point, 从当前指针之后安全遍历给定类型的链表
 * safe against removal of list entry.
 */
#define list_for_each_entry_safe_continue(pos, n, head, member) 		\
	for (pos = list_entry(pos->member.next, typeof(*pos), member), 		\
		n = list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))


/**
 * list_for_each_entry_safe_from   从当前指针开始安全遍历给定类型的链表
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 *
 * Iterate over list of given type from current point, safe against
 * removal of list entry.
 */
#define list_for_each_entry_safe_from(pos, n, head, member) 			\
	for (n = list_entry(pos->member.next, typeof(*pos), member);		\
	     &pos->member != (head);						\
	     pos = n, n = list_entry(n->member.next, typeof(*n), member))


/**
 * list_for_each_entry_safe_reverse
 * @pos:	the type * to use as a loop cursor.
 * @n:		another type * to use as temporary storage
 * @head:	the head for your list.
 * @member:	the name of the list_struct within the struct.
 *
 * Iterate backwards over list of given type, safe against removal   向后安全遍历整个链表
 * of list entry.
 */
#define list_for_each_entry_safe_reverse(pos, n, head, member)		\
	for (pos = list_entry((head)->prev, typeof(*pos), member),	\
		n = list_entry(pos->member.prev, typeof(*pos), member);	\
	     &pos->member != (head); 					\
	     pos = n, n = list_entry(n->member.prev, typeof(*n), member))


#endif
