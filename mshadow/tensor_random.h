#ifndef MSHADOW_TENSOR_RANDOM_H
#define MSHADOW_TENSOR_RANDOM_H
/*!
 *  \file tensor_random.h
 *  \brief Random inline functions for tensor.
 *  \author Bing Hsu, Tianqi Chen
 *   Based on curand|MKL|stdlib
 */
#include <cstdlib>
#include "tensor.h"

namespace mshadow {
    /*! \brief random number generator */
    template<typename device>
    class Random {};

    template<>
    class Random<cpu> {
    public:
        /*!
         * \constructor of random engine
         * \param seed random number seed
         */
        Random<cpu>( int seed ){
            #if MSHADOW_USE_MKL
            int status = vslNewStream(&vStream_, VSL_BRNG_MT19937, seed);
            utils::Assert( status == VSL_STATUS_OK, "MKL VSL Random engine failed to be initialized.\n" );
            #else
            srand(seed);
            #endif
            buffer_.shape[0] = kRandBufferSize;
            mshadow::AllocSpace( buffer_ );
        }
        ~Random<cpu>() {
            #if MSHADOW_USE_MKL
            vslDeleteStream(&vStream_);
            #endif
            mshadow::FreeSpace( buffer_ );
        }
        /*!
         * \brief generate data from uniform [a,b)
         * \param dst destination
         * \param a lower bound of uniform
         * \param b upper bound of uniform
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline void SampleUniform( Tensor<cpu, dim> &dst, real_t a=0.0f, real_t b=1.0f ) {
            Tensor<cpu, 2> mat = dst.FlatTo2D();
            for ( index_t i = 0; i < mat.shape[1]; ++i ) {
                #if MSHADOW_USE_MKL
                #if MSHADOW_SINGLE_PRECISION
                int status = vsRngUniform( 0, vStream_, mat.shape[0], mat[i].dptr, a, b );
                #else
                int status = vdRngUniform( 0, vStream_, mat.shape[0], mat[i].dptr, a, b );
                #endif
                utils::Assert(status == VSL_STATUS_OK, "Failed to generate random number by MKL.\n" );
                #else
                // use stdlib
                for ( index_t j = 0; j < mat.shape[0]; ++j ) {
                    mat[i][j] = this->RandNext()*(b-a) + a;
                }
                #endif
            }
        }
        /*!
         * \brief generate data from standard gaussian
         * \param dst destination
         * \param mu mean variable
         * \param sigma standard deviation
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline void SampleGaussian( Tensor<cpu, dim> &dst, real_t mu = 0.0f, real_t sigma = 1.0f ) {
            Tensor<cpu, 2> mat = dst.FlatTo2D();
            for (index_t i = 0; i < mat.shape[1]; ++i) {
                #if MSHADOW_USE_MKL
                #if MSHADOW_SINGLE_PRECISION
                int status = vsRngGaussian( 0, vStream_, mat.shape[0], mat[i].dptr, mu, sigma );
                #else
                int status = vdRngGaussian( 0, vStream_, mat.shape[0], mat[i].dptr, mu, sigma );
                #endif
                utils::Assert(status == VSL_STATUS_OK, "Failed to generate random number by MKL.\n" );
                #else
                real_t g1 = 0.0f, g2 = 0.0f;
                for (index_t j = 0; j < mat.shape[0]; ++j) {
                    if( (j & 1) == 0 ){
                        this->SampleNormal2D( g1, g2 );
                        mat[i][j] = mu + g1 * sigma;
                    }else{
                        mat[i][j] = mu + g2 * sigma;
                    }
                }
                #endif
            }
        }
        /*!
         * \brief return a temporal expression storing standard gaussian random variables
         *        the temporal tensor is only valid before next call of gaussian or uniform
         *        can be used as part of expression
         *  Caution: this means expression such as A = gaussian(s1) * gaussian(s2) will give invalid result,
         *           since second call of gaussian(s2) makes gaussian(s1) invalid
         *           A = gaussian(s1)*B+C; is correct; use one gaussian/uniform in each expression
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline expr::UnaryMapExp<op::identity,Tensor<cpu,dim>,expr::type::kMapper> gaussian( Shape<dim> shape ){
            Tensor<cpu,dim> temp = this->GetTemp( shape );
            this->SampleGaussian( temp, 0.0f, 1.0f );
            return expr::MakeExp<op::identity>( temp );
        }
        /*!
         * \brief return a temporal expression storing standard uniform [0,1)
         *        the temporal tensor is only valid before next call of gaussian or uniform
         *        can be used as part of expression
         *  Caution: this means expression such as A = gaussian(s1) * gaussian(s2) will give invalid result,
         *           since second call of gaussian(s2) makes gaussian(s1) invalid
         *           A = gaussian(s1)*B+C; is correct; use one gaussian/uniform in each expression
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline expr::UnaryMapExp<op::identity,Tensor<cpu,dim>,expr::type::kMapper> uniform( Shape<dim> shape ){
            Tensor<cpu,dim> temp = this->GetTemp( shape );
            this->SampleUniform( temp, 0.0f, 1.0f );
            return expr::MakeExp<op::identity>( temp );
        }
    private:
        /*!
         * \brief create temp storage from buffer with given shape
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline Tensor<cpu,dim> GetTemp( Shape<dim> shape ){
            shape.stride_ = ((shape[0] + 3) >> 2) << 2;
            utils::Assert( buffer_.shape[0] > shape.MSize(), "gaussian: random engine buffer do not have enough memory" );
            return Tensor<cpu,dim>( buffer_.dptr, shape );
        }
        /*! \brief get next random number from rand */
        inline real_t RandNext( void ){
            return static_cast<real_t>(rand()) / (static_cast<real_t>(RAND_MAX)+1.0f);
        }
        /*! \brief return a real numer uniform in (0,1) */
        inline real_t RandNext2( void ){
            return (static_cast<real_t>( rand() ) + 1.0 ) / (static_cast<real_t>(RAND_MAX) + 2.0);
        }
        /*!
         * \brief sample iid xx,yy ~N(0,1)
         * \param xx first  gaussian output
         * \param yy second gaussian output
         */
        inline void SampleNormal2D( real_t &xx, real_t &yy ){
            real_t x,y,s;
            do{
                x = 2.0f * RandNext2() - 1.0f;
                y = 2.0f * RandNext2() - 1.0f;
                s = x*x + y*y;
            }while( s >= 1.0f || s == 0.0f );
            real_t t = std::sqrt( -2.0f * std::log( s ) / s ) ;
            xx = x * t; yy = y * t;
        }
    private:
        #if MSHADOW_USE_MKL
        /*! \brief stream used by MKL VSL */
        VSLStreamStatePtr vStream_;
        #endif
        /*! \brief temporal space used to store random numbers */
        Tensor<cpu,1> buffer_;
    }; // class Random<cpu>

    template<>
    class Random<gpu> {
    public:
        /*!
         * \constructor of random engine
         * \param seed random number seed
         */
        Random<gpu>(int seed) {
            status_ = curandCreateGenerator(gen_, CURAND_RNG_PSEUDO_DEFAULT);
            utils::Assert(status_ == CURAND_STATUS_SUCCESS, "Can not create CURAND Generator\n");
            status_ = curandSetPseudoRandomGeneratorSeed(gen_, seed);
            utils::Assert(status_ == CURAND_STATUS_SUCCESS, "Set CURAND seed failed.\n");
            buffer_.shape[0] = kRandBufferSize; // Can be much smaller?
            mshadow::AllocSpace(buffer_);
        }

        ~Random<gpu>() {
            status_ = curandDestroyGenerator(gen_);
            utils::Assert(status_ == CURAND_STATUS_SUCCESS, "Destory CURAND Gen failed\n");
            mshadow::FreeSpace(buffer_);
        }

        /*!
         * \brief generate data from uniform [a,b)
         * \param dst destination
         * \param a lower bound of uniform
         * \param b upper bound of uniform
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline void SampleUniform(Tensor<gpu, dim> &dst, real_t a=0.0f, real_t b=1.0f) {
            Tensor<gpu, 2> = dst.FlatTo2D();
            real_t length = b - a;
            for (index_t i = 0; i < mat.shape[1]; ++i) {
                #if MSHADOW_SINGLE_PRECISION
                status_ = curandGenerateUniform(gen_, mat.shape[i].dptr, mat.shape[0]);
                #else
                status_ = curandGenerateUniformDouble(gen_, mat.shape[i].dptr, mat.shape[0]);
                #endif
                utils::Assert(status_ == CURAND_STATUS_SUCCESS, "CURAND Gen Uniform failed\n");
                cuda::TransferUniform(mat.shape[i].dptr, mat.shape[0], a, length);
            }
        }

        /*!
         * \brief generate data from standard gaussian
         * \param dst destination
         * \param mu mean variable
         * \param sigma standard deviation
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline void SampleGaussian(Tensor<gpu, dim> &dst, real_t mu = 0.0f, real_t sigma = 1.0f) {
            Tensor<gpu, 2> mat = dst.FlatTo2D();
            for (index_t i = 0; i < mat.shape[1]; ++i) {
                #if MSHADOW_SINGLE_PRECISION
                status_ = curandGenerateNormal(gen_, mat.shape[i].dptr, mat.shape[0], mu, sigma);
                #else
                status_ = curandGenerateNormalDouble(gen_, mat.shape[i].dptr, mat.shape[0], mu, sigma);
                #endif
                utils::Assert(status_ == CURAND_STATUS_SUCCESS, "CURAND Gen Gaussin failed\n");
            }
        }
        /*!
         * \brief return a temporal expression storing standard gaussian random variables
         *        the temporal tensor is only valid before next call of gaussian or uniform
         *        can be used as part of expression
         *  Caution: this means expression such as A = gaussian(s1) * gaussian(s2) will give invalid result,
         *           since second call of gaussian(s2) makes gaussian(s1) invalid
         *           A = gaussian(s1)*B+C; is correct; use one gaussian/uniform in each expression
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline expr::UnaryMapExp<op::identity,Tensor<gpu,dim>,expr::type::kMapper> gaussian(Shape<dim> shape){
            Tensor<gpu,dim> temp = this->GetTemp(shape);
            this->SampleGaussian(temp, 0.0f, 1.0f);
            return expr::MakeExp<op::identity>( temp );
        }
        /*!
         * \brief return a temporal expression storing standard uniform [0,1)
         *        the temporal tensor is only valid before next call of gaussian or uniform
         *        can be used as part of expression
         *  Caution: this means expression such as A = gaussian(s1) * gaussian(s2) will give invalid result,
         *           since second call of gaussian(s2) makes gaussian(s1) invalid
         *           A = gaussian(s1)*B+C; is correct; use one gaussian/uniform in each expression
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline expr::UnaryMapExp<op::identity,Tensor<gpu,dim>,expr::type::kMapper> uniform(Shape<dim> shape) {
            Tensor<gpu,dim> temp = this->GetTemp(shape);
            this->SampleUniform(temp, 0.0f, 1.0f);
            return expr::MakeExp<op::identity>(temp);
        }

    private:
        /*!
         * \brief create temp storage from buffer with given shape
         * \param shape shape of the tensor
         * \tparam dim dimension of tensor
         */
        template<int dim>
        inline Tensor<gpu,dim> GetTemp(Shape<dim> shape) {
            shape.stride_ = ((shape[0] + 3) >> 2) << 2;
            return Tensor<gpu,dim>(buffer_.dptr, shape);
        }
    private:
        curandGenerator_t gen_;
        curandStatus_t status_;
        Tensor<gpu, 1> buffer_;

    }; // class Random<gpu>




}; // namespace mshadow

#endif // MSHADOW_TENSOR_RANDOM_H
