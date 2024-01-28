#ifndef ORDERED_INTRUSIVE_LIST_H_
#define ORDERED_INTRUSIVE_LIST_H_
#include <micro-os-plus/utils/lists.h>

template <typename T, typename N, typename C, N T::*MP, C T::*CMP>
class ordered_intrusive_list : public micro_os_plus::utils::intrusive_list<T, N, MP> {
   public:
   using comparable = C;
   using iterator = micro_os_plus::utils::intrusive_list_iterator<T, N, MP>;
   using intrusive_list = micro_os_plus::utils::intrusive_list<T, N, MP>;
   using iterator_pointer = N *;
   /**
    * @}
    */

   /**
    * @name Constructors & Destructor
    * @{
    */

   ordered_intrusive_list() {
      intrusive_list::clear();
   }
   /**
    * @cond ignore
    */

   ordered_intrusive_list(const ordered_intrusive_list &) = delete;
   ordered_intrusive_list(ordered_intrusive_list &&) = delete;
   ordered_intrusive_list &operator=(const ordered_intrusive_list &) = delete;
   ordered_intrusive_list &operator=(ordered_intrusive_list &&) = delete;

   /**
    * @endcond
    */

   /**
    * @brief Destruct the list.
    */
   ~ordered_intrusive_list() {
   }

   /**
    * @}
    */

   public:
   /**
    * @name Public Member Functions
    * @{
    */

   /**
    * @brief Add a new thread node to the list.
    * @param [in] node Reference to a list node.
    * @par Returns
    *  Nothing.
    */
   void link(T &node);

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
      // Insert at the end of the list.
   } else if(node.*CMP >= *after.*CMP) {
      // Insert at the end of the list.
   } else if(node.*CMP < *intrusive_list::begin().*CMP) {
      // Insert at the beginning of the list.
      // At the beginning == after the end
      after = intrusive_list::end();
   } else {
      // Insert in the middle of the list.
      // The loop is guaranteed to terminate, and not hit the head.
      while(node.*CMP < *after.*CMP) {
         --after;
      }
   }
   intrusive_list::insert_after(node.*MP, &(*after.*MP));
}

#endif /* ORDERED_INTRUSIVE_LIST_H_ */
