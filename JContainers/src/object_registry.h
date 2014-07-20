#include <hash_set>
#include <hash_map>

namespace collections
{
    class object_registry
    {
    public:
        typedef std::hash_set<object_base *> all_objects_set;
        typedef std::hash_map<HandleT, object_base *> registry_container;
        typedef id_generator<HandleT> id_generator_type;

    private:

        friend class shared_state;

        registry_container _map;
        id_generator<HandleT> _idGen;
        all_objects_set _all_objects;
        mutable bshared_mutex _mutex;

        object_registry(const object_registry& );
        object_registry& operator = (const object_registry& );

    public:

        explicit object_registry()
            : _mutex()
        {
        }

        void registerNewObject(object_base& obj) {
            write_lock g(_mutex);
            auto itr = _all_objects.find(&obj);
            jc_assert(itr == _all_objects.end());
            _all_objects.insert(&obj);
        }

        void registerNewObjectId(object_base& obj) {
            jc_assert(obj._uid() == HandleNull);

            write_lock g(_mutex);

            auto id = _idGen.newId();
            jc_assert(_map.find(id) == _map.end());
            _map.insert(registry_container::value_type(id, &obj));
            obj._id = (Handle)id;
        }

        void removeObject(object_base& obj) {
            write_lock g(_mutex);

            auto id = obj._uid();
            if (id != HandleNull) {
                _map.erase(id);
                _idGen.reuseId(id);
            }

            auto itr = _all_objects.find(&obj);
            jc_assert(itr != _all_objects.end());
            _all_objects.erase(itr);
        }

        object_base *getObject(Handle hdl) const {
            if (!hdl) {
                return nullptr;
            }
            
            read_lock g(_mutex);
            return u_getObject(hdl);
        }

        std::vector<object_stack_ref> filter_objects(std::function<bool(object_base& obj)>& predicate) const {
            read_lock r(_mutex);

            std::vector<object_stack_ref> objects;

            for (auto obj : _all_objects) {
                if (predicate(*obj)) {
                    objects.push_back(obj);
                }
            }

            return objects;
        }

        object_stack_ref getObjectRef(Handle hdl) const {
            // had to copy&paste getObject function as we really must own an object BEFORE read lock will be released
            if (!hdl) {
                return nullptr;
            }
            read_lock g(_mutex);
            return u_getObject(hdl);
        }

        object_base *u_getObject(Handle hdl) const {
            if (!hdl) {
                return nullptr;
            }

            auto itr = _map.find(hdl);
            if (itr != _map.end())
                return itr->second;

            return nullptr;
        }

        template<class T>
        T *getObjectOfType(Handle hdl) const {
            auto obj = getObject(hdl);
            return obj->as<T>();
        }

        template<class T>
        T *u_getObjectOfType(Handle hdl) const {
            auto obj = u_getObject(hdl);
            return obj->as<T>();
        }

        void u_clear() {
            _map.clear();
            _idGen.u_clear();
            _all_objects.clear();
        }

        all_objects_set& u_container() {
            return _all_objects;
        }

        friend class boost::serialization::access;
        BOOST_SERIALIZATION_SPLIT_MEMBER();

        template<class Archive>
        void save(Archive & ar, const unsigned int version) const {
            jc_assert(version == 1);
            ar << _all_objects << _idGen;
        }

        template<class Archive>
        void load(Archive & ar, const unsigned int version) {

            switch (version) {
            default:
                jc_assert(false);
                break;
            case 1:
                ar >> _all_objects >> _idGen;

                for (auto& obj : _all_objects) {
                    if (obj->is_public()) {
                        _map.insert(registry_container::value_type(obj->_uid(), obj));
                    }
                }

                break;
            case 0: {
                typedef std::map<HandleT, object_base *> registry_container_old;
                registry_container_old oldCnt;
                ar >> oldCnt >> _idGen;

                _map.insert(oldCnt.begin(), oldCnt.end());

                std::transform(oldCnt.begin(), oldCnt.end(), std::inserter(_all_objects, _all_objects.begin()),
                    [](const registry_container_old::value_type& pair) {
                        return pair.second;
                    }
                );
            }
                break;
            }
        }
    };

    struct all_objects_lock
    {
        object_registry &_registry;

        explicit all_objects_lock(object_registry & registry)
            : _registry(registry)
        {
            for (auto& obj : registry.u_container()) {
                obj->_mutex.lock();
            }
        }

        ~all_objects_lock() {
            for (auto& obj : _registry.u_container()) {
                obj->_mutex.unlock();
            }
        }

    };


}

BOOST_CLASS_VERSION(collections::object_registry, 1);
