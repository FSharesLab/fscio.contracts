#pragma once

#include <fsciolib/fscio.hpp>
#include <fsciolib/ignore.hpp>
#include <fsciolib/transaction.hpp>

namespace fscio {

   class [[fscio::contract("fscio.wrap")]] wrap : public contract {
      public:
         using contract::contract;

         [[fscio::action]]
         void exec( ignore<name> executer, ignore<transaction> trx );

   };

} /// namespace fscio
