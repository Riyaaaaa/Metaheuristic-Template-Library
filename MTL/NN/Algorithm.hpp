//
//  Algorithm.hpp
//  MTL_Development
//
//  Created by Riya.Liel on 2015/07/14.
//  Copyright (c) 2015年 Riya.Liel. All rights reserved.
//

#ifndef MTL_Development_Algorithm_hpp
#define MTL_Development_Algorithm_hpp

#include<numeric>
#include"Utility.hpp"
#include"NNBase.hpp"
#include"../Common/configuration.h"

LIB_SCOPE_BEGIN()

struct threshold_af : ActivationFunc<threshold_af>{
    static double activate(double input,double T=0){ return input > T ? 1 : -1; }
    static double activateDerivative(double input); //no definition
};

struct rectified_linear_units_af : ActivationFunc<rectified_linear_units_af>{
    static double activate(double input){ return input >= 0 ? input : 0; }
    static double activateDerivative(double input); //no definition
};

struct no_activation_af : ActivationFunc<no_activation_af>{
    static double activate(double input){ return input; }
    static double activateDerivative(double input); //no definition
};

struct sigmoid_af : ActivationFunc<sigmoid_af>{
    static double activate(double input){ return 1 / (1. + exp(-input));}
    static double activateDerivative(double input){ return activate(input) * (1 - activate(input));}
	static constexpr float RANGE_MIN = 0;
	static constexpr float RANGE_MAX = 1;
};

struct tanh_af : ActivationFunc<mtl::tanh_af>{
    static double activate(double input){ return tanh(input);}
    static double activateDerivative(double input){ return 1 - std::pow(tanh(input),2);}
	static constexpr float RANGE_MIN = -1;
	static constexpr float RANGE_MAX = 1;
};

#ifdef GPU_ACCELERATION

struct sigmoid_af_gpu_accel : ActivationFunc<sigmoid_af> {
    static float activate(float input)restrict(cpu,amp) { return 1 / (1. + concurrency::fast_math::exp(-input)); }
    static float activateDerivative(float input)restrict(cpu,amp) { return input * (1 - input); }
    static constexpr float RANGE_MIN = 0;
    static constexpr float RANGE_MAX = 1;
};

struct tanh_af_gpu_accel : ActivationFunc<mtl::tanh_af> {
    static float activate_amp(float input)restrict(amp) { return concurrency::fast_math::fabs(input)>50 ? concurrency::precise_math::copysignf(1, input) : concurrency::fast_math::tanh(input);
        }
        static float activateDerivative_amp(float input)restrict(amp) { return 1 - activate_amp(input) * activate_amp(input); }
        static float activate(float input) restrict(cpu){ return tanhf(input); }
        static float activateDerivative(float input) restrict(cpu) { return 1 - std::pow(tanhf(input), 2); }
        static constexpr float RANGE_MIN = -1;
        static constexpr float RANGE_MAX = 1;
    };

#endif

template<class Layer, typename ActivationObject>
struct no_principle {
	const Layer& _layer;
	no_principle(const Layer& layer) :_layer(layer) {
	}

	auto operator[](std::size_t i) const{
		return _layer[i].output(ActivationObject::activate);
	}
};

template<class Layer, typename ActivationObject, bool isStdContainer>
struct _elite_principle;

template<class Layer,typename ActivationObject>
using elite_principle = _elite_principle<Layer, ActivationObject, is_container<Layer>::value>;

template<class Layer,typename ActivationObject>
struct _elite_principle<Layer,ActivationObject,true>{
	const Layer& _layer;
	std::size_t idx;
	_elite_principle(const Layer& layer) :_layer(layer){
		idx = std::max_element(layer.begin(), layer.end(), [](auto& lhs, auto& rhs) { return (lhs.getStatus() + lhs.bias) < (rhs.getStatus() + rhs.bias); }) - layer.begin();
	}

	auto operator[](std::size_t i)const {
		return i == idx ? _layer[i].output(ActivationObject::activate) : ActivationObject::RANGE_MIN;
	}
};

template<class Layer,typename ActivationObject>
struct _elite_principle<Layer,ActivationObject,false>{
	const Layer& _layer;
	int idx;
	_elite_principle(const Layer& layer):_layer(layer) {
		float max = layer[0].getStatus() + layer[0].bias;
		idx = 0;
		for (int i = 1; i < layer.get_extent()[0]; i++){
			if (max < layer[i].getStatus() + layer[i].bias) {
				idx = i;
				max = layer[i].getStatus() + layer[i].bias;
			}
		}
	}

	auto operator[](int i) const{
		return i == idx ? _layer[i].output(ActivationObject::activate) : ActivationObject::RANGE_MIN;
	}
};


template<class Tuple,class ActivationObject,class Tag>
struct _ErrorCorrection;

template<class NetworkStruct,class ActivationObject>
using ErrorCorrection = _ErrorCorrection<typename NetworkStruct::structure,ActivationObject,typename NetworkStruct::tag>;


template<class Tuple,class ActivationObject>
struct _ErrorCorrection<Tuple,ActivationObject,STATIC>{
    typedef std::remove_reference_t<Tuple> Tuple_t;
    typedef std::array<double,std::tuple_size<typename std::tuple_element<std::tuple_size<Tuple_t>::value-1,Tuple_t>::type >::value> output_layer_t;
    typedef ActivationObject actiavation_type;
    
    _ErrorCorrection(double trate) : _trate(trate){}
    const double _trate;
    actiavation_type ao;
    
    template<std::size_t Size1,std::size_t Size2>
    std::array<double,Size1> operator()(std::array<Unit<Size2>,Size1>& layer,const output_layer_t& target){
        double out;
        std::array<double,std::tuple_size<output_layer_t>::value> delta;
        
        for(std::size_t i=0; i<target.size() ; i++){
            out = layer[i].getStatus();
            //delta[i] = ao.activateDerivative(out) * (out - target[i]);
            delta[i] = (out - target[i]);
            layer[i].bias -= _trate * delta[i];
        }
        
        return delta;
    }
    
    template<std::size_t Size1,std::size_t Size2>
    void operator()(std::array<Unit<Size2>,Size1>& input_layer,const output_layer_t& target, std::array<double,Size2>&& delta){
        for(auto& unit: input_layer){
            for(int i=0; i<Size2; i++){
                unit.weight[i] -= _trate * delta[i] * unit.getStatus();
            }
        }
    }
};

template<class Tuple,class ActivationObject>
struct _ErrorCorrection<Tuple,ActivationObject,DYNAMIC>{
    typedef std::remove_reference_t<Tuple> Tuple_t;
    typedef std::array<double,std::tuple_size<typename std::tuple_element<std::tuple_size<Tuple_t>::value-1,Tuple_t>::type >::value> output_layer_t;
    typedef ActivationObject actiavation_type;
    
    _ErrorCorrection(double trate) : _trate(trate){}
    const double _trate;
    actiavation_type ao;
    
    template<std::size_t Size1,std::size_t Size2>
    std::array<double,Size1> operator()(std::array<Unit<Size2>,Size1>& layer,const output_layer_t& target){
        double out;
        std::array<double,std::tuple_size<output_layer_t>::value> delta;
        
        for(std::size_t i=0; i<target.size() ; i++){
            out = layer[i].getStatus() + layer[i].bias;
            delta[i] = ao.activateDerivative(out) * (target[i] - out);
            //delta[i] = (out - target[i]);
            layer[i].bias -= _trate * delta[i];
        }
        
        return delta;
    }
    
    template<std::size_t Size1,std::size_t Size2>
    void operator()(std::array<Unit<Size2>,Size1>& input_layer,const output_layer_t& target, std::array<double,Size2>&& delta){
        for(auto& unit: input_layer){
            for(int i=0; i<Size2; i++){
                unit.weight[i] -= _trate * delta[i] * unit.getStatus();
            }
        }
    }
};

template<class Tuple,class ActivationObject,class Tag,bool isSensory>
struct _Backpropagation;

template<class NetworkStruct,class ActivationObject,class Tag>
struct Backpropagation_traits;

template<class Tuple,class ActivationObject>
struct Backpropagation_traits<Tuple,ActivationObject,STATIC> {
    using type = _Backpropagation<Tuple, ActivationObject, STATIC, (std::tuple_size<Tuple>::value > 2)>;
};

template<class Tuple,class ActivationObject>
struct Backpropagation_traits<Tuple,ActivationObject,DYNAMIC> {
    using type = _Backpropagation<Tuple, ActivationObject, DYNAMIC, true>;
};
/*  Back propagation requires three or more layers */

template<class NetworkStruct,class ActivationObject>
using Backpropagation = typename Backpropagation_traits<typename NetworkStruct::structure,ActivationObject,typename NetworkStruct::tag>::type;

template<class Tuple,class ActivationObject>
struct _Backpropagation<Tuple,ActivationObject,STATIC,true>{
    typedef std::remove_reference_t<Tuple> Tuple_t;
    typedef std::array<double,std::tuple_size<typename std::tuple_element<std::tuple_size<Tuple_t>::value-1,Tuple_t>::type >::value> output_layer_t;
    
    _Backpropagation(double trate) : _trate(trate){}
    const double _trate;
    ActivationObject ao;
    
    template<std::size_t Size1,std::size_t Size2>
    std::array<double,Size1> operator()(std::array<Unit<Size2>,Size1>& layer,const output_layer_t& target){
        double out;
        std::array<double,std::tuple_size<output_layer_t>::value> delta;
        
        for(std::size_t i=0; i<target.size() ; i++){
            out = layer[i].getStatus();
            //delta[i] = (ao.activate(out) - target[i]);
            delta[i] = ao.activateDerivative(out) * (target[i] - ao.activate(out));
            layer[i].bias += _trate * delta[i];
        }
        
        return delta;
    }
    
    template<std::size_t Size1,std::size_t Size2>
    std::array<double,Size1> operator()(std::array<Unit<Size2>,Size1>& input_layer,const output_layer_t& target, const std::array<double,Size2>& delta){

        std::array<double,Size1> new_delta;
        
        for(int j=0; j<input_layer.size(); j++){
            double out, propagation = 0;
            
            for (int i = 0; i<delta.size(); i++) {
                input_layer[j].weight[i] += _trate * delta[i] * input_layer[j].getStatus();
            }
            
            for(int i=0; i<delta.size(); i++){
                propagation += input_layer[j].weight[i] * delta[i];
            }
            out = input_layer[j].getStatus();
            new_delta[j] = ao.activateDerivative(out) * propagation;
            input_layer[j].bias += _trate * new_delta[j];
            
        }
        
        return new_delta;
    }
};

template<class Tuple,class ActivationObject>
struct _Backpropagation<Tuple,ActivationObject,DYNAMIC,true>{
    typedef std::vector<float> output_layer_t;
    
	_Backpropagation(const float t_rate) :_trate(t_rate) {}
    const double _trate;
    ActivationObject ao;
    
    std::vector<double> operator()(std::vector<Unit_Dy>& layer,const output_layer_t& target){
        double out;
        std::vector<double> delta(target.size());
        
        for(std::size_t i=0; i<target.size() ; i++){
            out = layer[i].getStatus();
            //delta[i] = (target[i] - ao.activate(out));
            delta[i] = ao.activateDerivative(out) * (target[i] - layer[i].output(ao.activate));
            layer[i].bias += _trate * delta[i];
        }
        
        return delta;
    }

    std::vector<double> operator()(std::vector<Unit_Dy>& input_layer,const output_layer_t& target, const std::vector<double>& delta){
        
        std::vector<double> new_delta(input_layer.size());
        
        for(int j=0; j<input_layer.size(); j++){
			double out, propagation = 0;

			for (int i = 0; i<delta.size(); i++) {
				input_layer[j].weight[i] += _trate * delta[i] * input_layer[j].getStatus();
			}
			
            for(int i=0; i<delta.size(); i++){
                propagation += input_layer[j].weight[i] * delta[i];
            }
            out = input_layer[j].getStatus();
            new_delta[j] = ao.activateDerivative(out) * propagation;
            input_layer[j].bias += _trate * new_delta[j];

        }
        
        return new_delta;
    }
};

/* Network does not have three or more layers */
template<class Tuple,class ActivationObject,class Tag>
struct _Backpropagation<Tuple,ActivationObject,Tag,false>;
    
    
#ifdef GPU_ACCELERATION

template<class Net_t, class ActivationObject>
struct Backpropagation_Gpu_Accel{
	typedef concurrency::array_view<const float> output_layer_t;
	Backpropagation_Gpu_Accel(const float t_rate):_trate(t_rate) {}

	const float _trate;

	std::vector<float> operator()(concurrency::array_view<typename Net_t::Unit_t>& layer, const output_layer_t& target){
		ActivationObject ao;
		float out;
		float trate = _trate;
		std::vector<float> delta(target.get_extent()[0]);

		for (std::size_t i = 0; i<target.get_extent()[0]; i++) {
			out = layer[i].output(ao.activate);
			delta[i] = (target[i] - out);
			//delta[i] = ao.activateDerivative(out) * (target[i] - out);
			layer[i].bias += trate * delta[i];
		}
		
		return delta;
	}

	std::vector<float> operator()(concurrency::array_view<typename Net_t::Unit_t>& input_layer, const output_layer_t& target, const concurrency::array_view<const float>& delta) {
		ActivationObject ao;
		float trate = _trate;

		std::vector<float> new_delta(input_layer.get_extent()[0]);
		concurrency::array_view<float> new_delta_view(new_delta.size(), reinterpret_cast<float*>(&new_delta[0]));

		//gpu acceleration
		parallel_for_each(input_layer.get_extent(), [=](concurrency::index<1> idx)restrict(amp) {
			float out = input_layer[idx].getStatus() + input_layer[idx].bias, propagation = 0;
			for (int i = 0; i < delta.get_extent()[0]; i++) {
				input_layer[idx].weight[i] += trate * delta[i] * ao.activate_amp(out);
			}
			for (int i = 0; i < delta.get_extent()[0]; i++) {
				propagation += input_layer[idx].weight[i] * delta[i];
			}
			new_delta_view[idx] = ao.activateDerivative_amp(out) * propagation;
			input_layer[idx].bias += trate * new_delta_view[idx];
		});
		new_delta_view.synchronize();
		return new_delta;
	}
};

template<class Net_t, class ActivationObject>
struct Backpropagation_Convolution {
	typedef std::vector< typename Net_t::Status_t > output_layer_t;
	Backpropagation_Convolution(const float t_rate) :_trate(t_rate) {}

	const float _trate;

	std::vector<float> operator()(std::vector<typename Net_t::Unit_t>& layer, const output_layer_t& target) {
		ActivationObject ao;
		float out;
		std::vector< typename Net_t::Status_t > delta(target.size());

		for (std::size_t i = 0; i < target.size(); i++) {
			for (size_t j = 0; j < target[i].size(); j++) {
				out = ao.activate(layer[i].map[j]);
				delta[i][j] = (target[i][j] - out);
			}
			layer[i].bias += _trate * std::accumulate(delta[i].begin(), delta[i].end(), 0.0f);
		}

		return delta;
	}

	std::vector<float> operator()(std::vector<typename Net_t::Unit_t>& input_layer, const output_layer_t& target, const std::vector< Map<Net_t::FilterSize> >& delta) {
		ActivationObject ao;
		float trate = _trate;

		std::vector<float> new_delta(input_layer.size());

		//gpu acceleration
		for (int idx = 0; idx < input_layer.size(); idx++) {
			float out = input_layer[idx].getStatus() + input_layer[idx].bias, propagation = 0;
			for (int i = 0; i < delta.size(); i++) {
				for (int j = 0; j < delta[i].size(); j++) {
					for (int k = 0; k < Net_t::FilterSize) {
						for (int l = 0; l < Net_t::FilterSize; l++) {
							out = input_layer[idx].getStatus(j+l, i+k) + input_layer[idx].bias;
							input_layer[idx].weight[i][k][l] += trate * delta[i][j] * ao.activate(out);
							propagation += input_layer[idx].weight[i][k][l] * delta[i][j] * ao.activateDerivative(out);
						}
					}	
				}
			}
			new_delta[idx] = propagation;
			input_layer[idx].bias += trate * new_delta[idx];
		}
		return new_delta;
	}
};

#endif
    
LIB_SCOPE_END()
    
#endif

