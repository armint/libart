#ifndef ORDERED_INTRUSIVE_LIST_H_
#define ORDERED_INTRUSIVE_LIST_H_
#include <utils/lists.h>

/**
 * @brief An intrusive list that maintains its elements in a sorted order.
 *
 * This class extends `micro_os_plus::utils::intrusive_list` to provide
 * ordered insertion of elements. The order is determined by a comparable
 * member within the elements, specified by the `CMP` template parameter.
 *
 * @tparam T The type of the elements stored in the list.
 * @tparam N The type of the intrusive list node, typically a `micro_os_plus::utils::double_list_links`.
 * @tparam C The type of the comparable member within `T` used for ordering.
 * @tparam MP A pointer to a member variable of type `N` within `T`,
 *            representing the intrusive list links.
 * @tparam CMP A pointer to a member variable of type `C` within `T`,
 *             representing the comparable value for ordering.
 */
template <typename T, typename N, typename C, N T::*MP, C T::*CMP>
class ordered_intrusive_list : public micro_os_plus::utils::intrusive_list<T, N, MP> {
   public:
   using comparable = C;
   using iterator = micro_os_plus::utils::intrusive_list_iterator<T, N, MP>;
   using intrusive_list = micro_os_plus::utils::intrusive_list<T, N, MP>;
   using iterator_pointer = N *;

   /**
    * @brief Constructs an empty ordered intrusive list.
    *
    * Initializes the underlying intrusive list, clearing any previous state.
    */
   ordered_intrusive_list() {
      intrusive_list::clear();
   }

   ordered_intrusive_list(const ordered_intrusive_list &) = delete;
   ordered_intrusive_list(ordered_intrusive_list &&) = delete;
   ordered_intrusive_list &operator=(const ordered_intrusive_list &) = delete;
   ordered_intrusive_list &operator=(ordered_intrusive_list &&) = delete;

   /**
    * @brief Destructs the list.
    *
    * Note that this destructor does not deallocate the nodes themselves,
    * as this is an intrusive list and memory management of nodes is
    * external to the list.
    */
   ~ordered_intrusive_list() {
   }

   /**
    * @brief Inserts a new node into the list while maintaining sorted order.
    *
    * The node is inserted based on the value of its comparable member (`CMP`).
    * The list is traversed to find the correct insertion point to keep
    * elements sorted in ascending order (smallest `CMP` value first).
    *
    * @param [in] node Reference to the node to be linked into the list.
    * @par Returns
    *  Nothing.
    */
   void link(T &node);

   /**
    * @brief Returns an iterator to the last element in the list.
    *
    * @par Returns
    *  An iterator pointing to the last element. If the list is empty,
    *  this iterator will be equivalent to `end()`.
    */
   iterator tail() const {
      return iterator{ static_cast<iterator_pointer>(micro_os_plus::utils::static_double_list::head_.previous()) };
   }

   /**
    * @}
    */
};

template <typename T, typename N, typename C, N T::*MP, C T::*CMP>
void ordered_intrusive_list<T, N, C, MP, CMP>::link(T &node) {
   auto after = tail();
   if(intrusive_list::empty()) {
      // Insert at the end of the list, which is also the beginning when empty.
   } else if(node.*CMP >= *after.*CMP) {
      // If the new node's comparable value is greater than or equal to the
      // last element's, insert at the end of the list.
   } else if(node.*CMP < *intrusive_list::begin().*CMP) {
      // If the new node's comparable value is less than the first element's,
      // insert at the beginning of the list. This means inserting after the 'end' iterator.
      after = intrusive_list::end();
   } else {
      // Insert in the middle of the list.
      // Traverse backwards from the tail until a node with a smaller or
      // equal comparable value is found. The new node will be inserted after 'after'.
      // The loop is guaranteed to terminate, and not hit the head (sentinel node).
      while(node.*CMP < *after.*CMP) {
         --after;
      }
   }
   intrusive_list::insert_after(node.*MP, &(*after.*MP));
}

#endif /* ORDERED_INTRUSIVE_LIST_H_ */