/*
 * Copyright (c) 1996-2004 Barton P. Miller
 * 
 * We provide the Paradyn Parallel Performance Tools (below
 * described as "Paradyn") on an AS IS basis, and do not warrant its
 * validity or performance.  We reserve the right to update, modify,
 * or discontinue this software at any time.  We shall have no
 * obligation to supply such updates or modifications or any other
 * form of support to you.
 * 
 * This license is for research uses.  For such uses, there is no
 * charge. We define "research use" to mean you may freely use it
 * inside your organization for whatever purposes you see fit. But you
 * may not re-distribute Paradyn or parts of Paradyn, in any form
 * source or binary (including derivatives), electronic or otherwise,
 * to any other organization or entity without our permission.
 * 
 * (for other uses, please contact us at paradyn@cs.wisc.edu)
 * 
 * All warranties, including without limitation, any warranty of
 * merchantability or fitness for a particular purpose, are hereby
 * excluded.
 * 
 * By your use of Paradyn, you understand and agree that we (or any
 * other person or entity with proprietary rights in Paradyn) are
 * under no obligation to provide either maintenance services,
 * update services, notices of latent defects, or correction of
 * defects for Paradyn.
 * 
 * Even if advised of the possibility of such damages, under no
 * circumstances shall we (or any other person or entity with
 * proprietary rights in the software licensed hereunder) be liable
 * to you or any third party for direct, indirect, or consequential
 * damages of any character regardless of type of action, including,
 * without limitation, loss of profits, loss of use, loss of good
 * will, or computer failure or malfunction.  You agree to indemnify
 * us (and any other person or entity with proprietary rights in the
 * software licensed hereunder) for any and all liability it may
 * incur to third parties resulting from your use of Paradyn.
 */

#ifndef __ANNOTATABLE_H__
#define __ANNOTATABLE_H__

#if defined (MSC_VER)
#define DYN_DETAIL_BOOST_NO_INTRINSIC_WCHAR_T 1
#endif
#include <vector>
#include <map>
#include <typeinfo>
#include <string>
#include <string.h> // for strrchr()
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "dyntypes.h"
#include "util.h"
#include "dyn_detail/boost/type_traits/is_base_of.hpp"
#include "dyn_detail/boost/type_traits/is_pointer.hpp"
#include "dyn_detail/boost/type_traits/remove_pointer.hpp"
#define serialize_printf serializer_printf

namespace Dyninst
{


COMMON_EXPORT int serializer_printf(const char *format, ...);

typedef unsigned short AnnotationClassID;
typedef bool (*anno_cmp_func_t)(void *, void*);

extern int newAnnotationClass();
extern bool void_ptr_cmp_func(void *, void *);

class SerializerBase;
class Serializable;
typedef Serializable * (*ser_func_t) (void *, SerializerBase *, const char *);
COMMON_EXPORT Serializable * ser_func_wrapper(void *it, SerializerBase *sb,  const char *tag);

class AnnotationClassBase
{
   private:
      static std::vector<AnnotationClassBase *> *annotation_types;
      static dyn_hash_map<std::string, AnnotationClassID> *annotation_ids_by_name;
      anno_cmp_func_t cmp_func;
      AnnotationClassID id;
      std::string name;

   protected:

	  ser_func_t serialize_func;

      COMMON_EXPORT AnnotationClassBase(std::string n, 
            anno_cmp_func_t cmp_func_ = NULL, ser_func_t sf_ = NULL);

	  COMMON_EXPORT virtual ~AnnotationClassBase() {}

   public:

      COMMON_EXPORT static AnnotationClassBase *findAnnotationClass(unsigned int id);
      COMMON_EXPORT static void dumpAnnotationClasses();

      COMMON_EXPORT AnnotationClassID getID() { return id; }
      COMMON_EXPORT std::string &getName() {return name;}
      COMMON_EXPORT anno_cmp_func_t getCmpFunc() {return cmp_func;}
	  COMMON_EXPORT ser_func_t getSerializeFunc() {return serialize_func;}
	  COMMON_EXPORT virtual const char *getTypeName() = 0;
	  COMMON_EXPORT virtual void *allocate() = 0;
};

template <class T> 
class AnnotationClass : public AnnotationClassBase {
   public:

	  AnnotationClass(std::string n, 
			  anno_cmp_func_t cmp_func_ = NULL, 
			  ser_func_t s = NULL) :
		  AnnotationClassBase(n, cmp_func_, s)
	  {
		if (NULL == s)
		{
			//  if the type is Serializable, use its serialization function
			//  otherwise, leave it NULL so we don't accidentally dereference
			//  a random pointer as if it were automatcally descended from
			//  Serializable

			if (dyn_detail::boost::is_base_of<Serializable, T>::value)
			{
				serialize_func = ser_func_wrapper;
			}
			else if (dyn_detail::boost::is_pointer<T>::value)
			{
				if (dyn_detail::boost::is_base_of<Serializable, 
						typename dyn_detail::boost::remove_pointer<T>::type>::value)
				{
					serialize_func = ser_func_wrapper;
				}
			}
		}
	  }

	  const char *getTypeName() { return typeid(T).name();}
	  void *allocate() 
	  {
		  //  If T is (Serializable *) need to allocate a new object, not a new pointer
		  //  

		  if (dyn_detail::boost::is_pointer<T>::value)
		  {
				if (dyn_detail::boost::is_base_of<Serializable, 
						typename dyn_detail::boost::remove_pointer<T>::type>::value)
				{
					return (void *) new (typename dyn_detail::boost::remove_pointer<T>::type)();
				}
		  }

		  return (void *) new T();
	  }

	  size_t size() {return sizeof(T);}
	  bool isSparselyAnnotatable(); 
	  bool isDenselyAnnotatable(); 
};


typedef enum {
	    sparse,
		dense,
} sparse_or_dense_anno_t;

typedef struct {
	AnnotationClassBase *acb;
	void *data;
	void *parent_id;
	sparse_or_dense_anno_t sod;
} ser_rec_t;

typedef enum {
	sp_add_anno = 2,
	sp_rem_anno = 3,
	sp_add_cont_item = 4,
	sp_rem_cont_item = 5
} ser_post_op_t;

COMMON_EXPORT const char *serPostOp2Str(ser_post_op_t);


class AnnotatableDense;
class AnnotatableSparse;
COMMON_EXPORT bool is_input(SerializerBase *sb);
COMMON_EXPORT bool is_output(SerializerBase *sb);
COMMON_EXPORT unsigned short get_serializer_index(SerializerBase *sb);
COMMON_EXPORT void ser_operation(SerializerBase *, ser_post_op_t &, const char *);
COMMON_EXPORT bool add_annotations(SerializerBase *, AnnotatableDense *, std::vector<ser_rec_t> &);
COMMON_EXPORT bool add_annotations(SerializerBase *, AnnotatableSparse *, std::vector<ser_rec_t> &);
COMMON_EXPORT bool serialize_annotation_list(void *, std::vector<ser_rec_t> &, SerializerBase *sb, const char *);
COMMON_EXPORT SerializerBase *getExistingOutputSB(unsigned short);
COMMON_EXPORT bool serialize_post_annotation(void *, void *, SerializerBase *, AnnotationClassBase *acb, sparse_or_dense_anno_t, const char *);

class AnnotatableDense
{
	friend COMMON_EXPORT bool add_annotations(SerializerBase *, AnnotatableDense *, std::vector<ser_rec_t> &);
	friend class SerializerBase;
	friend class Serializable;
	typedef void *anno_list_t;

	/**
    * Inheriting from this class adds a pointer to each object.  Multiple
    * types of annotations are stored under this pointer in a 
    * annotation_type -> anno_list_t map.
    **/

   private:

      typedef anno_list_t anno_map_t;

      struct aInfo {
         anno_map_t *data;
         AnnotationClassID max;
		 unsigned short serializer_index;
      };

      aInfo *annotations;

	  //  private version of addAnnotation exists for deserialize functions
	  //  to reconstruct annotations without explicit type info -- don't use in 
	  //  other contexts

      COMMON_EXPORT bool addAnnotation(const void *a, AnnotationClassID id) 
	  {
         unsigned size = id + 1;
         if (!annotations)
         {
            annotations = (aInfo *) malloc(sizeof(aInfo));
			annotations->data = NULL;
			annotations->serializer_index = (unsigned short) -1;
		 }

		 //  can have case where we have allocated annotations struct but not the
		 //  actual annotations data array in case where we have performed serialization

		 if (annotations->data == NULL) 
		 {
			annotations->data = (anno_list_t *) calloc(sizeof(anno_list_t *), (size));
			annotations->max = size;
			for (unsigned i=0; i<size; i++)
				annotations->data[i] = NULL;
		 } 
		 else if (id >= annotations->max) 
		 {
			 int old_max = annotations->max;
			 size = annotations->max * 2;
			 annotations->max = size;
			 annotations->data = (anno_list_t *) realloc(annotations->data, sizeof(anno_list_t *) * size);
			 for (unsigned i=old_max; i<size; i++)
				 annotations->data[i] = NULL;
		 }

		 annotations->data[id] = const_cast<void *>(a);

         return true;
      }
   public:
      COMMON_EXPORT AnnotatableDense() : annotations(NULL)
      {
      }

	  COMMON_EXPORT ~AnnotatableDense()
	  {
		  if (annotations)
		  {
			  if (annotations->data)
				  free(annotations->data);
			  free(annotations);
		  }
	  }

      template<class T> 
      bool addAnnotation(const T *a, AnnotationClass<T> &a_id) 
      {

         int id = a_id.getID();
		 bool ret =  addAnnotation((void *)a, id);
		 if (!ret)
		 {
			 fprintf(stderr, "%s[%d]:  failed to add annotation\n", FILE__, __LINE__);
			 return ret;
		 }


		 //  If serialization is not enabled, there will be no serializer specified,
		 //  so none of the below code will be executed.

		 serialize_printf("%s[%d]:  %p addAnnotation:  serializer_index = %d\n", 
				 FILE__, __LINE__, this, annotations->serializer_index);

		 if (annotations && ( (unsigned short) -1 != annotations->serializer_index))
		 {
			 SerializerBase *sb = getExistingOutputSB(annotations->serializer_index);
			 if (!sb)
			 {
				 //  definitely should have a serializer since we have an index
				 fprintf(stderr, "%s[%d]:  FIXME:  no existing serializer!\n", FILE__, __LINE__);
				 return false;
			 }
			 ser_func_t sf = a_id.getSerializeFunc();
			 if (sf)
			 {
				 //  FIXME:  for xml support, ser_operation should have a corresponding 
				 //  "ser_operation_end()" routine to close out the xml field.
				 ser_post_op_t op = sp_add_anno;
				 ser_operation(sb, op, "AnnotationAdd");
				 void * aa = (void *) const_cast<T *>(a);
				 serialize_post_annotation(this, aa, sb, &a_id, dense, "PostAnnotation");
			 }
		 }

		 return true;
	  }


      template<class T> 
      inline bool getAnnotation(T *&a, AnnotationClass<T> &a_id) const
      {
         if (!annotations)
            return false;

         int id = a_id.getID();

         if (id > annotations->max) 
         {
            return false;
         }

         a = (T *) annotations->data[id];
         if (!a) return false;

         return true;
      }

      template<class T> 
      inline bool removeAnnotation(AnnotationClass<T> &a_id)
      {
         if (!annotations) return false;

         int id = a_id.getID();
         if (id > annotations->max) 
         {
            return false;
         }

         if (!annotations->data[id]) 
            return false;

         annotations->data[id] = NULL;

         return true;
      }

	  COMMON_EXPORT void serializeAnnotations(SerializerBase *sb, const char *tag)
	  {
		  serialize_printf("%s[%d]:  welcome to serializeAnotations:\n", FILE__, __LINE__);
		  //  iterator over possible annotation types
		  //  if we have any, lookup the serialization function and call it

		  std::vector<ser_rec_t> my_sers;
		  if (is_output(sb))
		  {
			  //  need to figure out how many annotations will be serialized apriori
			  //  so we can output the size of the list as a header.
			  //  To avoid iterating over the full list twice, make a local copy
			  //  of all serializations/deserializations that need to be performed
			  //  as we go, and do them in bulk afterwards.

			  if (annotations)
			  {
				  for (AnnotationClassID id = 0; id < annotations->max; ++id)
				  {
					  void *anno = annotations->data[id];
					  if (anno)
					  {
						  AnnotationClassBase *acb = AnnotationClassBase::findAnnotationClass(id);

						  if (!acb)
						  {
							  fprintf(stderr, "%s[%d]:  FIXME:  no annotation class for id %d\n", 
									  FILE__, __LINE__, id);
							  continue;
						  }

						  ser_func_t sf = acb->getSerializeFunc();

						  if (NULL != sf)
						  {
							  ser_rec_t sr;
							  sr.data = anno;
							  sr.acb = acb;
							  sr.parent_id = (void *) this;
							  sr.sod = dense;
							  my_sers.push_back(sr);
						  }
					  }
				  }

				  annotations->serializer_index = get_serializer_index(sb);
				  serialize_printf("%s[%d]:  %p set serializer index to %d\n", 
						  FILE__, __LINE__, this, annotations->serializer_index);
			  }
			  else
			  {
				  //  need to alloc struct to store serializer index
				  annotations = (aInfo *) malloc(sizeof(aInfo));
				  annotations->data = NULL;
				  annotations->max = 0;
				  annotations->serializer_index = get_serializer_index(sb);
				  serialize_printf("%s[%d]:  %p set serializer index to %d\n", 
						  FILE__, __LINE__, this, annotations->serializer_index);
			  }
		  }

		  if (!serialize_annotation_list(this, my_sers, sb, tag))
		  {
			  fprintf(stderr, "%s[%d]:  FIXME:  failed to serialize annotation list\n", 
					  FILE__, __LINE__);
		  }
		  if (!add_annotations(sb, this, my_sers))
		  {
			  fprintf(stderr, "%s[%d]:  failed to update annotation list after deserialize\n", 
					  FILE__, __LINE__);
		  }
	  }
};

class AnnotatableSparse
{
	friend COMMON_EXPORT bool add_annotations(SerializerBase *, AnnotatableSparse *, std::vector<ser_rec_t> &);
	friend class SerializerBase;
	friend class Serializable;
   public:
      struct void_ptr_hasher
      {
         size_t operator()(const void* a) const
         {
            return (size_t) a;
         }
      };

#if defined (_MSC_VER)
      typedef dyn_hash_map<void *, void *> annos_by_type_t;
#else
      typedef dyn_hash_map<void *, void *, void_ptr_hasher> annos_by_type_t;
#endif

      typedef std::vector<annos_by_type_t *> annos_t;

	  COMMON_EXPORT ~AnnotatableSparse()
	  {
		  //  We need to remove annotations from the static map when objects
		  //  are destroyed:  (1)  memory may be reclaimed and reused at the same
		  //  place, and (2) regardless of 1, the map can possibly explode to 
		  //  unmanageable sizes, with a lot of unused junk in it if a lot of i
		  //  annotatable objects are created and destroyed.

		  //  Alas this is kinda expensive right now, but the data structure is
		  //  set up to minimize search time, not deletion time.  It could
		  //  be changed if this becomes a significant time drain.

		  unsigned int n = 0;
		  for (unsigned int i = 0; i < annos.size(); ++i)
		  {
			  annos_by_type_t *abt = annos[i];
			  if (!abt) continue;

			  annos_by_type_t::iterator iter = abt->find(this);
			  if (iter != abt->end())
			  {
				  n++;
				  abt->erase(iter);
			  }
		  }
	  }

   private:

      COMMON_EXPORT static annos_t annos;
	  COMMON_EXPORT static dyn_hash_map<void *, unsigned short> ser_ndx_map;

      COMMON_EXPORT annos_by_type_t *getAnnosOfType(AnnotationClassID aid, bool do_create =false) const
	  {
         long nelems_to_create = aid - annos.size() + 1;

         if (nelems_to_create > 0)
         {
            if (!do_create)
            {
               return NULL;
            }

            while (nelems_to_create)
            {
               annos_by_type_t *newl = new annos_by_type_t();
               annos.push_back(newl);
               nelems_to_create--;
            }
         }

         annos_by_type_t *abt = annos[aid];

         return abt;
      }

      template <class T>
      inline annos_by_type_t *getAnnosOfType(AnnotationClass<T> &a_id, bool do_create =false) const
      {
         AnnotationClassID aid = a_id.getID();

		 return getAnnosOfType(aid, do_create);
	  }


      COMMON_EXPORT void *getAnnosForObject(annos_by_type_t *abt,
            void *obj, bool do_create = false) const 
      {
         assert(abt);
         assert(obj);

         void  *target = NULL;

         annos_by_type_t::iterator iter = abt->find(obj);

         if (iter == abt->end())
         {
            if (!do_create)
            {
               return NULL;
            }

            (*abt)[obj] = target;
         }
         else
         {
            target = iter->second;
         }

         return target;
      }

	  //  private version of addAnnotation used by deserialize function to restore
	  //  annotation set without explicitly specifying types
	  COMMON_EXPORT inline bool addAnnotation(const void *a, AnnotationClassID aid)
	  {
		  void *obj = this;
		  annos_by_type_t *abt = getAnnosOfType(aid, true /*do create if needed*/);
		  assert(abt);

		  annos_by_type_t::iterator iter = abt->find(obj);
		  if (iter == abt->end())
		  {
			  (*abt)[obj] = const_cast<void *>(a);
		  }
		  else
		  {
			  //  do silent replacement -- this case can arise if an annotatable object
			  //  is destroyed and then reallocated as a new object at the same address.
			  //  Given the sparse map data structure, it would be rather expensive to 
			  //  go through the map in the dtor to eliminate references to the 
			  //  destroyed object.

			  //  Added annotation removal in dtor...  so this case should _not_ arise,
			  //  do the replacement, but make some noise:

			  if (a != iter->second)
			  {
				  serialize_printf("%s[%d]:  WEIRD:  already have annotation of this type: %p, replacing with %p\n", FILE__, __LINE__, iter->second, a);
				  iter->second = const_cast<void *>(a);
			  }

			  return true;
		  }

		  return true;
	  }

   public:

	  COMMON_EXPORT bool operator==(AnnotatableSparse &cmp)
	  {
		  unsigned this_ntypes = annos.size();
         unsigned cmp_ntypes = cmp.annos.size();
         unsigned ntypes = (cmp_ntypes > this_ntypes) ? cmp_ntypes : this_ntypes;

         for (unsigned int i = 0; i < ntypes; ++i) 
         {
            if ((i >= cmp_ntypes) || (i >= this_ntypes)) 
            {
               //  compare is done since at least one set of annotations
               //  has been exhausted
               break;
            }

            annos_by_type_t *this_abt = annos[i];
            annos_by_type_t *cmp_abt = cmp.annos[i];

            if (!this_abt) 
            {
               fprintf(stderr, "%s[%d]:  WEIRD: FIXME\n", FILE__, __LINE__);
               continue;
            }

            if (!cmp_abt) 
            {
               fprintf(stderr, "%s[%d]:  WEIRD: FIXME\n", FILE__, __LINE__);
               continue;
            }

            annos_by_type_t::iterator this_abt_iter = this_abt->find(this);
            annos_by_type_t::iterator cmp_abt_iter = cmp_abt->find(&cmp);

            //  if one has annotations of this particular type and other other
            //  doesn't, then we are def. not equal, so fail:

            if (this_abt_iter == this_abt->end())
            {
               if (cmp_abt_iter != cmp_abt->end())
               {
                  return false;
               }

               //  both are at end()
               continue;
            }

            if (   (cmp_abt_iter == cmp_abt->end())
                  && (this_abt_iter != this_abt->end()))
            {
               return false;
            }

            AnnotationClassBase *acb = AnnotationClassBase::findAnnotationClass(i);

            if (!acb)
            {
               serialize_printf("%s[%d]:  cannot find annotation class for id %d\n", 
                     FILE__, __LINE__, i);
               return false;
            }

            //  both have annotation -- do the compare
            anno_cmp_func_t cmpfunc = acb->getCmpFunc();

            if (!cmpfunc)
            {
               //  even if not explicitly specified, a default pointer-compare
               //  function should be returned here.

               fprintf(stderr, "%s[%d]:  no cmp func for anno id %d\n", 
                     FILE__, __LINE__, i);
               return false;
            }

            void *arg1 = cmp_abt_iter->second;
            void *arg2 = this_abt_iter->second;

            bool ret = (*cmpfunc)(arg1, arg2);

            return ret;
         }

         return true;
      }

      template<class T>
      inline bool addAnnotation(const T *a, AnnotationClass<T> &a_id)
         {
            void *obj = this;
            annos_by_type_t *abt = getAnnosOfType(a_id, true /*do create if needed*/);
            assert(abt);

            annos_by_type_t::iterator iter = abt->find(obj);
            if (iter == abt->end())
            {
               (*abt)[obj] = (void *) const_cast<T *>(a);
            }
            else
            {
				//  do silent replacement -- this case can arise if an annotatable object
				//  is destroyed and then reallocated as a new object at the same address.
				//  Given the sparse map data structure, it would be rather expensive to 
				//  go through the map in the dtor to eliminate references to the 
				//  destroyed object.

				//  Added annotation removal in dtor...  so this case should _not_ arise,
				//  do the replacement, but make some noise:

				if (iter->second != a) 
				{
					//fprintf(stderr, "%s[%d]:  WEIRD:  already have annotation of type %s: %p, replacing with %p\n", FILE__, __LINE__, a_id.getName().c_str(), iter->second, a);
					iter->second = (void *)const_cast<T *>(a);
				}

				return true;
            }

			dyn_hash_map<void *, unsigned short>::iterator seriter;
			seriter = ser_ndx_map.find(this);
			if (seriter != ser_ndx_map.end())
			{
				if (seriter->second != (unsigned short) -1)
				{
					SerializerBase *sb = getExistingOutputSB(seriter->second);
					if (!sb)
					{
						fprintf(stderr, "%s[%d]:  FIXME:  no existing output SB\n", 
								FILE__, __LINE__);
						return false;
					}

					ser_func_t sf = a_id.getSerializeFunc();
					if (sf)
					{
						ser_post_op_t op = sp_add_anno;
						ser_operation(sb, op, "AnnotationAdd");
						void *aa = (void *) const_cast<T *>(a);
						serialize_post_annotation(this, aa, sb, &a_id, sparse, "PostAnnotation");
					}
				}
			}
				
            return true;
         }

      template<class T>
      inline bool getAnnotation(T *&a, AnnotationClass<T> &a_id) const 
      {
         a = NULL;

         annos_by_type_t *abt = getAnnosOfType(a_id, false /*don't create if none*/);

         if (!abt)
         {
            return false;
         }

         AnnotatableSparse * this_noconst = const_cast<AnnotatableSparse *>(this);
         void *annos_for_object = getAnnosForObject(abt, (void *)this_noconst,
               false /*no create if none*/);

         if (!annos_for_object)
         {
            return false;
         }

         a = (T *)annos_for_object;
         return true;
      }

	  template<class T>
	  inline bool removeAnnotation(AnnotationClass<T> &a_id)
	  {
		  void *obj = this;
		  annos_by_type_t *abt = getAnnosOfType(a_id, false /*do create if needed*/);
		  assert(abt);

		  annos_by_type_t::iterator iter = abt->find(obj);
		  if (iter == abt->end())
		  {
			  //  annotation does not exist, so we return false (remove failed)
			  return false;
		  }
		  else
		  {
			  abt->erase(iter);
			  return true;
		  }
	      return false;
	  }

	  COMMON_EXPORT void serializeAnnotations(SerializerBase *sb, const char *tag)
	  {
		  std::vector<ser_rec_t> my_sers;
            void *obj = this;
			if (is_output(sb))
			{
				for (AnnotationClassID id = 0; id < annos.size(); ++id)
				{
					annos_by_type_t *abt = getAnnosOfType(id, false /*don't do create */);
					if (NULL == abt) continue;

					annos_by_type_t::iterator iter = abt->find(obj);

					if (iter == abt->end()) 
					{
						//	fprintf(stderr, "%s[%d]:  nothing for this obj\n", FILE__, __LINE__);
						continue;
					}

					//  we have an annotation of this type for this object, find the serialization
					//  function and call it (if it exists)

					AnnotationClassBase *acb =  AnnotationClassBase::findAnnotationClass(id);
					if (!acb)
					{
						serialize_printf("%s[%d]:  FIXME, cannot find annotation class base for id %d, mode = %s\n", FILE__, __LINE__, id, is_input(sb) ? "deserialize" : "serialize");
						continue;
					}

					ser_func_t sf = acb->getSerializeFunc();

					if (NULL == sf)
					{
						serialize_printf("%s[%d]:  no serialization function for this anno: type: %s, name: %s\n", FILE__, __LINE__, acb->getTypeName(), acb->getName().c_str());
						continue;
					}

					ser_rec_t sr;
					sr.acb = acb;
					sr.data = iter->second;
					sr.parent_id = (void *) this;
					sr.sod = sparse;
					my_sers.push_back(sr);
				}

				if (my_sers.size())
					serialize_printf("%s[%d]:  found %lu req'd serializations for obj %p\n", 
							FILE__, __LINE__, my_sers.size(), this);

				ser_ndx_map[this] = get_serializer_index(sb);
			}

			if (!serialize_annotation_list(this, my_sers, sb, tag))
			{
				fprintf(stderr, "%s[%d]:  FIXME:  failed to serialize annotation list\n", 
						FILE__, __LINE__);
			}

			if (!add_annotations(sb, this, my_sers))
			{
				fprintf(stderr, "%s[%d]:  failed to update annotation list after deserialize\n", 
						FILE__, __LINE__);
			}
	  }
};

} // namespace

#endif
