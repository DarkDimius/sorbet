#include "Symbols.h"
#include "Context.h"
#include "Types.h"
#include "core/Names/core.h"
#include <sstream>
#include <string>

template class std::vector<ruby_typer::core::TypeAndOrigins>;
template class std::vector<ruby_typer::core::LocalVariable>;
template class std::vector<std::pair<ruby_typer::core::NameRef, ruby_typer::core::SymbolRef>>;

namespace ruby_typer {
namespace core {

using namespace std;

bool SymbolRef::operator==(const SymbolRef &rhs) const {
    return _id == rhs._id;
}

bool SymbolRef::operator!=(const SymbolRef &rhs) const {
    return !(rhs == *this);
}

std::vector<std::shared_ptr<core::Type>> Symbol::selfTypeArgs(const GlobalState &gs) const {
    ENFORCE(isClass()); // should be removed when we have generic methods
    std::vector<shared_ptr<core::Type>> targs;
    for (auto tm : typeMembers()) {
        if (tm.data(gs).isFixed()) {
            targs.emplace_back(tm.data(gs).resultType);
        } else {
            targs.emplace_back(make_shared<core::SelfTypeParam>(tm));
        }
    }
    return targs;
}
std::shared_ptr<core::Type> Symbol::selfType(const GlobalState &gs) const {
    ENFORCE(isClass());
    // todo: in dotty it made sense to cache those.
    if (typeMembers().empty()) {
        return make_shared<core::ClassType>(ref(gs));
    } else {
        return make_shared<core::AppliedType>(ref(gs), selfTypeArgs(gs));
    }
}

std::shared_ptr<core::Type> Symbol::externalType(const GlobalState &gs) const {
    ENFORCE(isClass());
    // todo: also cache these?
    if (typeMembers().empty()) {
        return make_shared<ClassType>(ref(gs));
    } else {
        std::vector<shared_ptr<Type>> targs;
        for (auto tm : typeMembers()) {
            if (tm.data(gs).isFixed()) {
                targs.emplace_back(tm.data(gs).resultType);
            } else {
                targs.emplace_back(Types::dynamic());
            }
        }
        return make_shared<AppliedType>(ref(gs), targs);
    }
}

bool Symbol::derivesFrom(const GlobalState &gs, SymbolRef sym) const {
    // TODO: add baseClassSet
    for (SymbolRef a : argumentsOrMixins) {
        if (a == sym || a.data(gs).derivesFrom(gs, sym)) {
            return true;
        }
    }
    if (this->superClass.exists()) {
        return sym == this->superClass || this->superClass.data(gs).derivesFrom(gs, sym);
    }
    return false;
}

SymbolRef Symbol::ref(const GlobalState &gs) const {
    auto distance = this - gs.symbols.data();
    return SymbolRef(gs, distance);
}

Symbol &SymbolRef::data(GlobalState &gs, bool allowNone) const {
    ENFORCE(_id < gs.symbols.size());
    if (!allowNone) {
        ENFORCE(this->exists());
    }

    return gs.symbols[this->_id];
}

const Symbol &SymbolRef::data(const GlobalState &gs, bool allowNone) const {
    ENFORCE(_id < gs.symbols.size());
    if (!allowNone) {
        ENFORCE(this->exists());
    }

    return gs.symbols[this->_id];
}

bool SymbolRef::isSynthetic() const {
    return this->_id < Symbols::MAX_SYNTHETIC_SYMBOLS;
}

bool SymbolRef::isHiddenFromPrinting(const GlobalState &gs) const {
    if (isSynthetic()) {
        return true;
    }
    auto loc = data(gs).definitionLoc;
    return loc.file.id() < 0 || loc.file.data(gs).source_type == File::Payload;
}

void printTabs(ostringstream &to, int count) {
    int i = 0;
    while (i < count) {
        to << "  ";
        i++;
    }
}

string SymbolRef::toString(const GlobalState &gs, int tabs, bool showHidden) const {
    ostringstream os;
    const Symbol &myInfo = data(gs, true);
    string name = myInfo.name.toString(gs);
    auto &members = myInfo.members;

    vector<string> children;
    children.reserve(members.size());
    for (auto pair : members) {
        if (pair.first == Names::singleton() || pair.first == Names::attached()) {
            continue;
        }

        auto str = pair.second.toString(gs, tabs + 1, showHidden);
        if (!str.empty()) {
            children.push_back(str);
        }
    }

    if (!showHidden && this->isHiddenFromPrinting(gs) && children.empty()) {
        return "";
    }

    printTabs(os, tabs);

    string type = "unknown";
    if (myInfo.isClass()) {
        type = "class";
    } else if (myInfo.isStaticField()) {
        type = "static-field";
    } else if (myInfo.isField()) {
        type = "field";
    } else if (myInfo.isMethod()) {
        type = "method";
    } else if (myInfo.isMethodArgument()) {
        type = "argument";
    } else if (myInfo.isTypeMember()) {
        type = "typeMember";
    } else if (myInfo.isTypeArgument()) {
        type = "type argument";
    }

    if (myInfo.isTypeArgument() || myInfo.isTypeMember()) {
        char variance;
        if (myInfo.isCovariant()) {
            variance = '+';
        } else if (myInfo.isContravariant()) {
            variance = '-';
        } else if (myInfo.isInvariant()) {
            variance = '=';
        } else {
            Error::raise("type without variance");
        }
        type = type + "(" + variance + ")";
    }

    os << type << " " << name;
    if (myInfo.isClass() || myInfo.isMethod()) {
        if (myInfo.isMethod()) {
            if (myInfo.isPrivate()) {
                os << " : private";
            } else if (myInfo.isProtected()) {
                os << " : protected";
            }
        }

        auto typeMembers = myInfo.isClass() ? myInfo.typeMembers() : myInfo.typeArguments();
        if (!typeMembers.empty()) {
            os << "[";
            bool first = true;
            for (SymbolRef thing : typeMembers) {
                if (first) {
                    first = false;
                } else {
                    os << ", ";
                }
                os << thing.data(gs).name.toString(gs);
            }
            os << "]";
        }

        if (myInfo.superClass.exists()) {
            os << " < " << myInfo.superClass.data(gs).fullName(gs);
        }
        os << " (";
        bool first = true;
        for (SymbolRef thing : myInfo.argumentsOrMixins) {
            if (first) {
                first = false;
            } else {
                os << ", ";
            }
            os << thing.data(gs).name.toString(gs);
        }
        os << ")";
    }
    if (myInfo.isMethodArgument()) {
        vector<pair<int, const char *>> methodFlags = {
            {Symbol::Flags::ARGUMENT_OPTIONAL, "optional"},
            {Symbol::Flags::ARGUMENT_KEYWORD, "keyword"},
            {Symbol::Flags::ARGUMENT_REPEATED, "repeated"},
            {Symbol::Flags::ARGUMENT_BLOCK, "block"},
        };
        os << "<";
        bool first = true;
        for (auto &flag : methodFlags) {
            if ((myInfo.flags & flag.first) != 0) {
                if (first) {
                    first = false;
                } else {
                    os << ", ";
                }
                os << flag.second;
            }
        }
        os << ">";
    }
    if (myInfo.resultType) {
        os << " -> " << myInfo.resultType->toString(gs, tabs);
    }
    os << endl;

    sort(children.begin(), children.end());
    for (auto row : children) {
        os << row;
    }
    return os.str();
}

SymbolRef::SymbolRef(const GlobalState &from, u4 _id) : _id(_id) {}
SymbolRef::SymbolRef(GlobalState const *from, u4 _id) : _id(_id) {}

SymbolRef Symbol::findMember(const GlobalState &gs, NameRef name) const {
    histogramInc("find_member_scope_size", members.size());
    for (auto &member : members) {
        if (member.first == name) {
            return member.second.data(gs).dealias(gs);
        }
    }
    return Symbols::noSymbol();
}

SymbolRef Symbol::findMemberTransitive(const GlobalState &gs, NameRef name, int maxDepth) const {
    ENFORCE(this->isClass());
    if (maxDepth == 0) {
        gs.logger.critical("findMemberTransitive hit a loop while resolving {} in {}. Parents are: ", name.toString(gs),
                           this->fullName(gs));
        int i = -1;
        for (auto it = this->argumentsOrMixins.rbegin(); it != this->argumentsOrMixins.rend(); ++it) {
            i++;
            gs.logger.critical("{}:- {}", i, it->data(gs).fullName(gs));
            int j = 0;
            for (auto it2 = it->data(gs).argumentsOrMixins.rbegin(); it2 != it->data(gs).argumentsOrMixins.rend();
                 ++it2) {
                gs.logger.critical("{}:{} {}", i, j, it2->data(gs).fullName(gs));
                j++;
            }
        }

        Error::raise("findMemberTransitive hit a loop while resolving ");
    }

    SymbolRef result = findMember(gs, name);
    if (result.exists()) {
        return result;
    }
    for (auto it = this->argumentsOrMixins.rbegin(); it != this->argumentsOrMixins.rend(); ++it) {
        ENFORCE(it->exists());
        result = it->data(gs).findMemberTransitive(gs, name, maxDepth - 1);
        if (result.exists()) {
            return result;
        }
    }
    if (this->superClass.exists()) {
        return this->superClass.data(gs).findMemberTransitive(gs, name, maxDepth - 1);
    }
    return Symbols::noSymbol();
}

string Symbol::fullName(const GlobalState &gs) const {
    string owner_str;
    if (this->owner.exists() && this->owner != core::Symbols::root()) {
        owner_str = this->owner.data(gs).fullName(gs);
    }

    if (this->isClass()) {
        return owner_str + "::" + this->name.toString(gs);
    } else {
        return owner_str + "#" + this->name.toString(gs);
    }
}

SymbolRef Symbol::singletonClass(GlobalState &gs) {
    ENFORCE(this->isClass());
    ENFORCE(this->name.data(gs).isClassName(gs));

    SymbolRef selfRef = this->ref(gs);
    if (selfRef == Symbols::untyped()) {
        return Symbols::untyped();
    }

    SymbolRef singleton = findMember(gs, Names::singleton());
    if (singleton.exists()) {
        return singleton;
    }

    NameRef singletonName = gs.freshNameUnique(UniqueNameKind::Singleton, this->name, 1);
    singleton = gs.enterClassSymbol(this->definitionLoc, this->owner, singletonName);
    Symbol &singletonInfo = singleton.data(gs);

    counterInc("singleton_classes");
    singletonInfo.members.push_back(make_pair(Names::attached(), selfRef));
    singletonInfo.superClass = core::Symbols::todo();
    singletonInfo.setIsModule(false);

    selfRef.data(gs).members.push_back(make_pair(Names::singleton(), singleton));
    return singleton;
}

SymbolRef Symbol::attachedClass(const GlobalState &gs) const {
    ENFORCE(this->isClass());
    if (this->ref(gs) == Symbols::untyped()) {
        return Symbols::untyped();
    }

    SymbolRef singleton = findMember(gs, Names::attached());
    return singleton;
}

SymbolRef Symbol::dealias(const GlobalState &gs) const {
    if (auto alias = cast_type<AliasType>(resultType.get())) {
        return alias->symbol.data(gs).dealias(gs);
    }
    return this->ref(gs);
}

bool Symbol::isBlockSymbol(const GlobalState &gs) const {
    const core::Name &nm = name.data(gs);
    return nm.kind == NameKind::UNIQUE && nm.unique.original == Names::blockTemp();
}

SymbolRef SymbolRef::dealiasAt(GlobalState &gs, core::SymbolRef klass) const {
    ENFORCE(data(gs).isTypeMember());
    if (data(gs).owner == klass) {
        return *this;
    } else {
        SymbolRef cursor;
        if (data(gs).owner.data(gs).derivesFrom(gs, klass)) {
            cursor = data(gs).owner;
        } else if (klass.data(gs).derivesFrom(gs, data(gs).owner)) {
            cursor = klass;
        }
        while (true) {
            if (!cursor.exists()) {
                return cursor;
            }
            for (auto aliasPair : cursor.data(gs).typeAliases) {
                if (aliasPair.first == *this) {
                    return aliasPair.second.dealiasAt(gs, klass);
                }
            }
            cursor = cursor.data(gs).superClass;
        }
    }
}

Symbol Symbol::deepCopy(const GlobalState &to) const {
    Symbol result;
    result.owner = this->owner;
    result.flags = this->flags;
    result.argumentsOrMixins = this->argumentsOrMixins;
    result.resultType = this->resultType;
    result.name = NameRef(to, this->name.id());
    result.definitionLoc = this->definitionLoc;
    result.definitionLoc.file = FileRef(to, this->definitionLoc.file.id());
    result.typeParams = this->typeParams;
    result.typeAliases = this->typeAliases;

    result.members.reserve(this->members.size());
    for (auto &mem : this->members) {
        result.members.emplace_back(NameRef(to, mem.first.id()), mem.second);
    }
    result.superClass = this->superClass;
    result.uniqueCounter = this->uniqueCounter;
    return result;
}

int Symbol::typeArity(const GlobalState &gs) const {
    ENFORCE(this->isClass());
    int arity = 0;
    for (auto &ty : this->typeMembers()) {
        if (!ty.data(gs).isFixed()) {
            ++arity;
        }
    }
    return arity;
}

void Symbol::sanityCheck(const GlobalState &gs) const {
    if (!debug_mode) {
        return;
    }
    SymbolRef current = this->ref(gs);
    if (current != Symbols::root()) {
        SymbolRef current2 =
            const_cast<GlobalState &>(gs).enterSymbol(this->definitionLoc, this->owner, this->name, this->flags);
        ENFORCE(current == current2);
    }
}

SymbolRef Symbol::enclosingMethod(const GlobalState &gs) const {
    if (isMethod()) {
        return ref(gs);
    }
    SymbolRef owner = this->owner;
    while (owner != Symbols::root() && !owner.data(gs, false).isMethod()) {
        ENFORCE(owner.exists(), "non-existing owner in enclosingMethod");
        owner = owner.data(gs).owner;
    }
    return owner;
}

SymbolRef Symbol::enclosingClass(const GlobalState &gs) const {
    SymbolRef owner = ref(gs);
    while (owner != Symbols::root() && !owner.data(gs, false).isClass()) {
        ENFORCE(owner.exists(), "non-existing owner in enclosingClass");
        owner = owner.data(gs).owner;
    }
    if (owner == Symbols::root()) {
        return Symbols::noSymbol();
    }
    return owner;
}

LocalVariable::LocalVariable(NameRef name, u4 unique) : _name(name), unique(unique) {}
LocalVariable::LocalVariable() : _name() {}
bool LocalVariable::exists() const {
    return _name._id > 0;
}

bool LocalVariable::isSyntheticTemporary(const GlobalState &gs) const {
    if (_name.data(gs).kind == NameKind::UNIQUE)
        return true;
    if (unique == 0) {
        return false;
    }
    return _name == core::Names::whileTemp() || _name == core::Names::ifTemp() || _name == core::Names::returnTemp() ||
           _name == core::Names::statTemp() || _name == core::Names::assignTemp() ||
           _name == core::Names::returnMethodTemp() || _name == core::Names::blockReturnTemp() ||
           _name == core::Names::selfMethodTemp() || _name == core::Names::hashTemp() ||
           _name == core::Names::arrayTemp() || _name == core::Names::rescueTemp() ||
           _name == core::Names::castTemp() || _name == core::Names::finalReturn();
}

bool LocalVariable::isAliasForGlobal(const GlobalState &gs) const {
    return _name == core::Names::cfgAlias();
}

bool LocalVariable::operator==(const LocalVariable &rhs) const {
    return this->_name == rhs._name && this->unique == rhs.unique;
}

bool LocalVariable::operator!=(const LocalVariable &rhs) const {
    return !this->operator==(rhs);
}
std::string LocalVariable::toString(const core::GlobalState &gs) const {
    if (unique == 0) {
        return this->_name.toString(gs);
    }
    return this->_name.toString(gs) + "$" + std::to_string(this->unique);
}

} // namespace core
} // namespace ruby_typer
