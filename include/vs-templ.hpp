#pragma once

#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <map>
#include <optional>
#include <stack>
#include <string>
#include <variant>
#include <vector>

#include <pugixml.hpp>

#include "utils.hpp"
#include "symbols.hpp"
#include "logging.hpp"

namespace vs{
namespace templ{


struct preprocessor{
    private:
        std::string ns_prefix = "ns:";

        //Final document to be shared
        pugi::xml_document compiled;

        //To track operations within the parser
        std::stack<pugi::xml_node> stack_compiled;
        std::stack<pugi::xml_node_iterator> stack_data;
        std::stack<std::pair<pugi::xml_node_iterator,pugi::xml_node_iterator>> stack_template;

        std::vector<log> logs;

        //Stack-like table of symbols
        symbol_map symbols;

        //Entry point in the root document.
        pugi::xml_node root_data;

    public:
        inline preprocessor(const pugi::xml_document& root_data, const pugi::xml_document& root_template, const char* prefix="s:"){
            init(root_data,root_template,prefix);
        }

        void init(const pugi::xml_document& root_data, const pugi::xml_document& root_template, const char* prefix="s:");
        void reset();

        inline pugi::xml_document& parse(){_parse({});return compiled;}
        inline void ns(const char* str){ns_prefix = str;strings.prepare(str);}

    private:
        struct order_method_t{
            enum values{
                UNKNOWN =  0, 
                ASC, 
                DESC, 
                RANDOM,

                USE_DOT_EVAL = 16 //For strings, split evaluation based on their dot groups. Valid for all methods.
            };

            static values from_string(const char* str);
        };

        //Precomputed string to avoid spawning an absurd number of small objects in heap at each cycle.
        struct ns_strings{
            private:
                char* data = nullptr;
            public:

            //S:TAGS
            const char *FOR_RANGE_TAG;

            const char *FOR_TAG;
            const char *FOR_PROPS_TAG;
                const char *EMPTY_TAG;
                const char *HEADER_TAG;
                const char *FOOTER_TAG;
                const char *ITEM_TAG;
                const char *ERROR_TAG;

            const char *WHEN_TAG;
                const char *IS_TAG;

            const char *VALUE_TAG;
            const char *ELEMENT_TAG;
                const char *TYPE_ATTR;

            //S:PROPS
            const char *FOR_IN_PROP;
            const char *FOR_FILTER_PROP;
            const char *FOR_SORT_BY_PROP;
            const char *FOR_ORDER_BY_PROP;
            const char *FOR_OFFSET_PROP;
            const char *FOR_LIMIT_PROP;

            const char *FOR_PROPS_IN_PROP;
            const char *FOR_PROPS_FILTER_PROP;
            const char *FOR_PROPS_ORDER_BY_PROP;
            const char *FOR_PROPS_OFFSET_PROP;
            const char *FOR_PROPS_LIMIT_PROP;

            const char *VALUE_SRC_PROP;
            const char *VALUE_FORMAT_PROP;

            const char *USE_SRC_PROP;

            void prepare(const char * ns_prefix);

            inline ~ns_strings(){if(data!=nullptr)delete[] data;}

        }strings;

        //Transforming a string into a parsed symbol, setting an optional base root or leaving it to a default evaluation.
        std::optional<concrete_symbol> resolve_expr(const char* _str, const pugi::xml_node* base=nullptr);

        std::vector<pugi::xml_attribute> prepare_props_data(const pugi::xml_node& base, int limit, int offset, bool(*filter)(const pugi::xml_attribute&), order_method_t::values criterion);

        std::vector<pugi::xml_node> prepare_children_data(const pugi::xml_node& base, int limit, int offset, bool(*filter)(const pugi::xml_node&), const std::vector<std::pair<std::string,order_method_t::values>>& criteria);

        void _parse(std::optional<pugi::xml_node_iterator> stop_at){ 
            
            while(!stack_template.empty()){

                auto& current_template = stack_template.top();
                auto& current_compiled = stack_compiled.top();

                if(stop_at.has_value() && current_template.first==stop_at)break;

                if(current_template.first!=current_template.second){

                    //Special handling of static element
                    if(strncmp(current_template.first->name(),ns_prefix.c_str(),ns_prefix.length())==0) {
                        if(strcmp(current_template.first->name(),strings.FOR_RANGE_TAG)==0){
                            const char* tag = current_template.first->attribute("tag").as_string();
                            int from = get_or<int>(resolve_expr(current_template.first->attribute("from").as_string("0")).value_or(0),0);
                            int to = get_or<int>(resolve_expr(current_template.first->attribute("to").as_string("0")).value_or(0),0);
                            int step = get_or<int>(resolve_expr(current_template.first->attribute("step").as_string("1")).value_or(1),1);
                            if(step>0 && to<from){/* Skip infinite loop*/}
                            else if(step<0 && to>from){/* Skip infinite loop*/}
                            else if(step==0){/* Skip potentially infinite loop*/}
                            else for(int i=from; i<to; i+=step){
                                auto frame_guard = symbols.guard();
                                if(tag!=nullptr)symbols.set(tag,i);
                                symbols.set("$",i);
                                stack_template.push({current_template.first->begin(),current_template.first->end()});
                                _parse(current_template.first);
                                //When exiting one too many is removed. restore it.
                                stack_compiled.push(current_compiled);
                            }
                        }
                        else if(strcmp(current_template.first->name(),strings.FOR_TAG)==0){
                            const char* tag = current_template.first->attribute("tag").as_string();
                            const char* in = current_template.first->attribute("in").as_string();
                            //TODO: filter has not defined syntax yet.
                            const char* _filter = current_template.first->attribute("filter").as_string();

                            const char* _sort_by = current_template.first->attribute("sort-by").as_string();
                            const char* _order_by = current_template.first->attribute("order-by").as_string("asc");

                            int limit = get_or<int>(resolve_expr(current_template.first->attribute("limit").as_string("0")).value_or(0),0);
                            int offset = get_or<int>(resolve_expr(current_template.first->attribute("offset").as_string("0")).value_or(0),0);
                            
                            auto expr = resolve_expr(in);


                            //Only a node is acceptable in this context, otherwise show the error
                            if(!expr.has_value() || !std::holds_alternative<const pugi::xml_node>(expr.value())){ 
                                for(const auto& el: current_template.first->children(strings.ERROR_TAG)){
                                    stack_template.push({el.begin(),el.end()});
                                    _parse(current_template.first);
                                    stack_compiled.push(current_compiled);
                                }
                            }
                            else{
                                std::vector<std::pair<std::string,order_method_t>> criteria;
                                //Build criteria
                                {
                                    auto orders = split(_order_by,',');
                                    int c = 0;
                                    //Apply order directive with wrapping in case not enough cases are specified.
                                    for(auto& i:split(_sort_by,',')){criteria.push_back({i,order_form_str(orders[c%orders.size()])});c++;}
                                }

                                auto good_data = prepare_children_data(std::get<const pugi::xml_node>(expr.value()), limit, offset, nullptr, criteria);

                                if(good_data.size()==0){
                                    for(const auto& el: current_template.first->children(strings.EMPTY_TAG)){
                                        stack_template.push({el.begin(),el.end()});
                                        _parse(current_template.first);
                                        stack_compiled.push(current_compiled);
                                    }
                                }
                                else{
                                    //Header (once)
                                    {
                                        for(const auto& el: current_template.first->children(strings.HEADER_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }
                                    }
                                
                                    //Items (iterate)
                                    for(auto& i : good_data){
                                        auto frame_guard = symbols.guard();
                                        if(tag!=nullptr)symbols.set(tag,i);
                                        symbols.set("$",i);
                                        for(const auto& el: current_template.first->children(strings.ITEM_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }

                                    }

                                    //Footer (once)
                                    {
                                        for(const auto& el: current_template.first->children(strings.FOOTER_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }
                                    }
                                }
                            }
                        }
                        else if(strcmp(current_template.first->name(),strings.FOR_PROPS_TAG)==0){
                            const char* tag = current_template.first->attribute("tag").as_string();
                            const char* in = current_template.first->attribute("in").as_string();
                            //TODO: filter has not defined syntax yet.
                            const char* _filter = current_template.first->attribute("filter").as_string();

                            const char* _order_by = current_template.first->attribute("order-by").as_string("asc");

                            int limit = get_or<int>(resolve_expr(current_template.first->attribute("limit").as_string("0")).value_or(0),0);
                            int offset = get_or<int>(resolve_expr(current_template.first->attribute("offset").as_string("0")).value_or(0),0);
                            
                            auto expr = resolve_expr(in);


                            //Only a node is acceptable in this context, otherwise show the error
                            if(!expr.has_value() || !std::holds_alternative<const pugi::xml_node>(expr.value())){ 
                                for(const auto& el: current_template.first->children(strings.ERROR_TAG)){
                                    stack_template.push({el.begin(),el.end()});
                                    _parse(current_template.first);
                                    stack_compiled.push(current_compiled);
                                }
                            }
                            else{
                                auto good_data = prepare_props_data(std::get<const pugi::xml_node>(expr.value()), limit, offset, nullptr,order_method_t::from_string(_order_by));

                                if(good_data.size()==0){
                                    for(const auto& el: current_template.first->children(strings.EMPTY_TAG)){
                                        stack_template.push({el.begin(),el.end()});
                                        _parse(current_template.first);
                                        stack_compiled.push(current_compiled);
                                    }
                                }
                                else{
                                    //Header (once)
                                    {
                                        for(const auto& el: current_template.first->children(strings.HEADER_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }
                                    }
                                
                                    //Items (iterate)
                                    for(auto& i : good_data){
                                        auto frame_guard = symbols.guard();
                                        if(tag!=nullptr)symbols.set(tag,i);
                                        symbols.set("$",i);
                                        for(const auto& el: current_template.first->children(strings.ITEM_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }

                                    }

                                    //Footer (once)
                                    {
                                        for(const auto& el: current_template.first->children(strings.FOOTER_TAG)){
                                            stack_template.push({el.begin(),el.end()});
                                            _parse(current_template.first);
                                            stack_compiled.push(current_compiled);
                                        }
                                    }
                                }
                            }
                        }
                        else if(strcmp(current_template.first->name(), strings.ELEMENT_TAG)==0){
                            //It is possible for it to generate strange results as strings are not validated by pugi
                            auto symbol = resolve_expr(current_template.first->attribute(strings.TYPE_ATTR).as_string("$"));
                            if(!symbol.has_value()){
                            }
                            else if(std::holds_alternative<std::string>(symbol.value())){
                                auto child = current_compiled.append_child(std::get<std::string>(symbol.value()).c_str());
                                for(auto& attr : current_template.first->attributes()){
                                    if(strcmp(attr.name(),strings.TYPE_ATTR)!=0)child.append_attribute(attr.name()).set_value(attr.value());
                                }
                                stack_compiled.push(child);

                                stack_template.push({current_template.first->begin(),current_template.first->end()});
                                _parse(current_template.first);
                                stack_compiled.push(current_compiled);
                            }
                            else if(std::holds_alternative<const pugi::xml_node>(symbol.value())){
                                auto child = current_compiled.append_child(std::get<const pugi::xml_node>(symbol.value()).text().as_string());
                                for(auto& attr : current_template.first->attributes()){
                                    if(strcmp(attr.name(),strings.TYPE_ATTR)!=0)child.append_attribute(attr.name()).set_value(attr.value());
                                }
                                stack_compiled.push(child);

                                stack_template.push({current_template.first->begin(),current_template.first->end()});
                                _parse(current_template.first);
                                stack_compiled.push(current_compiled);
                            }
                            else{}
                        }
                        else if(strcmp(current_template.first->name(), strings.VALUE_TAG)==0){
                            auto symbol = resolve_expr(current_template.first->attribute("src").as_string("$"));
                            if(!symbol.has_value()){
                                /*Show default content if search fails*/
                                stack_template.push({current_template.first->begin(),current_template.first->end()});
                                _parse(current_template.first);
                                stack_compiled.push(current_compiled);
                            }
                            else{
                                if(std::holds_alternative<int>(symbol.value())){
                                    current_compiled.append_child(pugi::node_pcdata).set_value(std::to_string(std::get<int>(symbol.value())).c_str());
                                }
                                else if(std::holds_alternative<const pugi::xml_attribute>(symbol.value())) {
                                    current_compiled.append_child(pugi::node_pcdata).set_value(std::get<const pugi::xml_attribute>(symbol.value()).as_string());
                                }
                                else if(std::holds_alternative<std::string>(symbol.value())) {
                                    current_compiled.append_child(pugi::node_pcdata).set_value(std::get<std::string>(symbol.value()).c_str());
                                }
                                else if(std::holds_alternative<const pugi::xml_node>(symbol.value())) {
                                    auto tmp = std::get<const pugi::xml_node>(symbol.value());
                                    current_compiled.append_copy(tmp);
                                }
                            }
                        }
                        else if(strcmp(current_template.first->name(),strings.WHEN_TAG)==0){
                            auto subject = resolve_expr(current_template.first->attribute("subject").as_string("$"));
                            for(const auto& entry: current_template.first->children(strings.IS_TAG)){
                                bool _continue =  entry.attribute("continue").as_bool(false);
                                auto test = resolve_expr(entry.attribute("value").as_string("$"));

                                bool result = false;
                                //TODO: Perform comparison.

                                if(!subject.has_value() && !test.has_value()){result = true;}
                                else if (!subject.has_value() || !test.has_value()){result = false;}
                                else if(std::holds_alternative<int>(subject.value()) && std::holds_alternative<int>(test.value())){
                                    result = std::get<int>(subject.value())==std::get<int>(test.value());
                                }
                                else{

                                    //Move everything to string
                                    const char* op1,* op2;
                                    if(std::holds_alternative<std::string>(subject.value()))op1=std::get<std::string>(subject.value()).c_str();
                                    else if(std::holds_alternative<const pugi::xml_attribute>(subject.value()))op1=std::get<const pugi::xml_attribute>(subject.value()).as_string();
                                    else if(std::holds_alternative<const pugi::xml_node>(subject.value()))op1=std::get<const pugi::xml_node>(subject.value()).text().as_string();

                                    if(std::holds_alternative<std::string>(test.value()))op2=std::get<std::string>(test.value()).c_str();
                                    else if(std::holds_alternative<const pugi::xml_attribute>(test.value()))op2=std::get<const pugi::xml_attribute>(test.value()).as_string();
                                    else if(std::holds_alternative<const pugi::xml_node>(test.value()))op2=std::get<const pugi::xml_node>(test.value()).text().as_string();

                                    result = strcmp(op1,op2)==0;
                                }
                        
                                if(result){
                                    stack_template.push({entry.begin(),entry.end()});
                                    _parse(current_template.first);
                                    stack_compiled.push(current_compiled);

                                    if(_continue==false)break;
                                }
                            }
                        }
                        else {std::cerr<<"unrecognized static operation\n";}
                        
                        current_template.first++;
                        continue;
                    }
                
                    

                    auto last = current_compiled.append_child(current_template.first->type());
                    last.set_name(current_template.first->name());
                    last.set_value(current_template.first->value());
                    for(const auto& attr :current_template.first->attributes()){
                        //Special handling of static attribute rewrite rules
                        if(strncmp(attr.name(), ns_prefix.c_str(), ns_prefix.length())==0){
                            if(cexpr_strneqv(attr.name()+ns_prefix.length(),"for.src.")){}
                            else if(cexpr_strneqv(attr.name()+ns_prefix.length(),"for-prop.src.")){}
                            else if(cexpr_strneqv(attr.name()+ns_prefix.length(),"use.src.")){}
                            else if(cexpr_strneqv(attr.name()+ns_prefix.length(),"eval.")){}
                            else {std::cerr<<"unrecognized static operation\n";}
                        }
                        else last.append_attribute(attr.name()).set_value(attr.value());
                    }
                    if(!current_template.first->children().empty()){
                
                        stack_template.push({current_template.first->children().begin(),current_template.first->children().end()});
                        stack_compiled.push(last);
                    }
                    current_template.first++;
                }
                else{
                    stack_template.pop();
                    stack_compiled.pop();
                }
            }

            return;
        }

};


}
}