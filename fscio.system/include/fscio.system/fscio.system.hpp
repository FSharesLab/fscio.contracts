/**
 *  @file
 *  @copyright defined in fsc/LICENSE.txt
 */
#pragma once

#include <fscio.system/native.hpp>
#include <fsciolib/asset.hpp>
#include <fsciolib/time.hpp>
#include <fsciolib/privileged.hpp>
#include <fsciolib/singleton.hpp>
#include <fscio.system/exchange_state.hpp>

#include <string>
#include <type_traits>
#include <optional>

namespace fsciosystem {

   using fscio::name;
   using fscio::asset;
   using fscio::symbol;
   using fscio::symbol_code;
   using fscio::indexed_by;
   using fscio::const_mem_fun;
   using fscio::block_timestamp;
   using fscio::time_point;
   using fscio::microseconds;
   using fscio::datastream;

   template<typename E, typename F>
   static inline auto has_field( F flags, E field )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, bool>
   {
      return ( (flags & static_cast<F>(field)) != 0 );
   }

   template<typename E, typename F>
   static inline auto set_field( F flags, E field, bool value = true )
   -> std::enable_if_t< std::is_integral_v<F> && std::is_unsigned_v<F> &&
                        std::is_enum_v<E> && std::is_same_v< F, std::underlying_type_t<E> >, F >
   {
      if( value )
         return ( flags | static_cast<F>(field) );
      else
         return ( flags & ~static_cast<F>(field) );
   }

   struct [[fscio::table, fscio::contract("fscio.system")]] name_bid {
     name            newname;
     name            high_bidder;
     int64_t         high_bid = 0; ///< negative high_bid == closed auction waiting to be claimed
     time_point      last_bid_time;

     time_point      reserved1;
     uint64_t        reserved2;
     uint64_t        reserved3;

     uint64_t primary_key()const { return newname.value;                    }
     uint64_t by_high_bid()const { return static_cast<uint64_t>(-high_bid); }
   };

   struct [[fscio::table, fscio::contract("fscio.system")]] bid_refund {
      name         bidder;
      asset        amount;

      time_point   reserved1;
      uint64_t     reserved2;
      uint64_t     reserved3;

      uint64_t primary_key()const { return bidder.value; }
   };

   typedef fscio::multi_index< "namebids"_n, name_bid,
                               indexed_by<"highbid"_n, const_mem_fun<name_bid, uint64_t, &name_bid::by_high_bid>  >
                             > name_bid_table;

   typedef fscio::multi_index< "bidrefunds"_n, bid_refund > bid_refund_table;

   struct [[fscio::table("global"), fscio::contract("fscio.system")]] fscio_global_state : fscio::blockchain_parameters {
      uint64_t free_ram()const { return max_ram_size - total_ram_bytes_reserved; }

      uint64_t             max_ram_size = 64ll*1024 * 1024 * 1024;
      uint64_t             total_ram_bytes_reserved = 0;
      int64_t              total_ram_stake = 0;

      block_timestamp      last_producer_schedule_update;
      time_point           last_pervote_bucket_fill;
      int64_t              pervote_bucket = 0;
      int64_t              perblock_bucket = 0;
      uint32_t             total_unpaid_blocks = 0; /// all blocks which have been produced but not paid
      int64_t              total_activated_stake = 0;
      time_point           thresh_activated_stake_time;
      uint16_t             last_producer_schedule_size = 0;
      double               total_producer_vote_weight = 0; /// the sum of all producer votes
      block_timestamp      last_name_close;
      uint16_t             new_ram_per_block = 0;
      block_timestamp      last_ram_increase;
      block_timestamp      last_block_num; /* deprecated */
      double               total_producer_votepay_share = 0;
      double               total_producer_blockpay_share = 0;
      uint8_t              revision = 0; ///< used to track version updates in the future.
      time_point           last_vpay_state_update;
      double               total_vpay_share_change_rate = 0;
      time_point           last_bpay_state_update;
      double               total_bpay_share_change_rate = 0;
      asset                res_airdrop_limit_net;
      asset                res_airdrop_limit_cpu;
      uint32_t             res_airdrop_limit_ram_bytes = 0;

      time_point           reserved1;
      time_point           reserved2;
      time_point           reserved3;
      uint64_t             reserved4;
      uint64_t             reserved5;
      uint64_t             reserved6;
      uint64_t             reserved7;
      uint64_t             reserved8;
      uint64_t             reserved9;

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE_DERIVED( fscio_global_state, fscio::blockchain_parameters,
                                (max_ram_size)(total_ram_bytes_reserved)(total_ram_stake)
                                (last_producer_schedule_update)(last_pervote_bucket_fill)
                                (pervote_bucket)(perblock_bucket)(total_unpaid_blocks)(total_activated_stake)(thresh_activated_stake_time)
                                (last_producer_schedule_size)(total_producer_vote_weight)(last_name_close)(new_ram_per_block)
                                (last_ram_increase)(last_block_num)(total_producer_votepay_share)(total_producer_blockpay_share)(revision) 
                                (last_vpay_state_update)(total_vpay_share_change_rate)(last_bpay_state_update)(total_bpay_share_change_rate)
                                (res_airdrop_limit_net)(res_airdrop_limit_cpu)(res_airdrop_limit_ram_bytes)
                                (reserved1)(reserved2)(reserved3)(reserved4)(reserved5)(reserved6)(reserved7)(reserved8)(reserved8)
                              )
   };

   struct [[fscio::table, fscio::contract("fscio.system")]] producer_info {
      name                  owner;
      std::vector<name>     voters;
      double                total_votes = 0;
      fscio::public_key     producer_key; /// a packed public key object
      bool                  is_active = true;
      std::string           url;
      uint32_t              unpaid_blocks = 0;
      time_point            last_claim_time;
      uint16_t              location = 0;
      double                votepay_share = 0;
      time_point            last_votepay_share_update;
      double                blockpay_share = 0;
      time_point            last_blockpay_share_update;
      double                commission_rate = 0;
      time_point            last_commission_rate_adjustment_time;
      int128_t              total_voteage;
      fscio::asset          total_vote_num;
      time_point            voteage_update_time;
      int64_t               rewards_producer_block_pay_balance = 0;
      int64_t               rewards_producer_vote_pay_balance = 0;
      int64_t               rewards_voters_block_pay_balance = 0;
      int64_t               rewards_voters_vote_pay_balance = 0;

      time_point            reserved1;
      time_point            reserved2;
      uint64_t              reserved3;
      uint64_t              reserved4;
      uint64_t              reserved5;
      uint64_t              reserved6;

      uint64_t primary_key()const { return owner.value;                             }
      double   by_votes()const    { return is_active ? -total_votes : total_votes;  }
      bool     active()const      { return is_active;                               }
      void     deactivate()       { producer_key = public_key(); is_active = false; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( producer_info, (owner)(voters)(total_votes)(producer_key)(is_active)(url)
                        (unpaid_blocks)(last_claim_time)(location)
                        (votepay_share)(last_votepay_share_update)(blockpay_share)(last_blockpay_share_update)
                        (commission_rate)(last_commission_rate_adjustment_time)(total_voteage)(total_vote_num)
                        (voteage_update_time)(rewards_producer_block_pay_balance)(rewards_producer_vote_pay_balance)
                        (rewards_voters_block_pay_balance)(rewards_voters_vote_pay_balance)
                        (reserved1)(reserved2)(reserved3)(reserved4)(reserved5)(reserved6)
                      )
   };

   struct [[fscio::table, fscio::contract("fscio.system")]] voter_info {
      name                 owner;     /// the voter
      fscio::asset         staked_balance;
      /**
       *  Every time a vote is cast we must first "undo" the last vote weight, before casting the
       *  new vote weight.  Vote weight is calculated as:
       *
       *  stated.amount * 2 ^ ( weeks_since_launch/weeks_per_year)
       */
      double              last_vote_weight = 0; /// the vote weight cast the last time the vote was updated

      time_point           last_claim_time;
      uint32_t             flags1 = 0;

      time_point           reserved1;
      time_point           reserved2;
      uint64_t             reserved3;
      uint64_t             reserved4;
      uint64_t             reserved5;
      uint64_t             reserved6;

      uint64_t             primary_key() const { return owner.value; }

      enum class flags1_fields : uint32_t {
         ram_managed = 1,
         net_managed = 2,
         cpu_managed = 4
      };
      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( voter_info, (owner)(staked_balance)(last_vote_weight)(last_claim_time)(flags1)
                                    (reserved1)(reserved2)(reserved3)(reserved4)(reserved5)(reserved6)
                      )
   };

   struct [[fscio::table, fscio::contract("fscio.system")]] vote_info {
      name                 producer_name; 
      fscio::asset         vote_num;
      double               vote_weight;
      time_point           voteage_update_time;
      int128_t             voteage = 0;

      time_point           reserved1;
      uint64_t             reserved2;
      uint64_t             reserved3;

      uint64_t             primary_key() const { return producer_name.value; }

      FSCLIB_SERIALIZE( vote_info, (producer_name)(vote_num)(vote_weight)(voteage_update_time)(voteage)
                                   (reserved1)(reserved2)(reserved3)
                      )
   };

   struct [[fscio::table, fscio::contract("fscio.system")]] res_airdrop_info {
      name                owner;            /// the accept airdrop user
      fscio::asset        res_airdrop_net;  /// airdropped net
      fscio::asset        res_airdrop_cpu;  /// airdropped cpu
      uint32_t            res_airdrop_ram;  /// airdropped ram

      time_point          reserved1;
      uint64_t            reserved2;
      uint64_t            reserved3;

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( res_airdrop_info, (owner)(res_airdrop_net)(res_airdrop_cpu)(res_airdrop_ram)
                                          (reserved1)(reserved2)(reserved3)
                      )  
   };

   typedef fscio::multi_index<"resad"_n, res_airdrop_info> res_airdrop_table; 

   typedef fscio::multi_index< "voters"_n, voter_info >  voters_table;
   typedef fscio::multi_index< "votes"_n, vote_info >  votes_table;


   typedef fscio::multi_index< "producers"_n, producer_info,
                               indexed_by<"prototalvote"_n, const_mem_fun<producer_info, double, &producer_info::by_votes>  >
                             > producers_table;

   typedef fscio::singleton< "global"_n, fscio_global_state >   global_state_singleton;

   //   static constexpr uint32_t     max_inflation_rate = 5;  // 5% annual inflation
   static constexpr uint32_t     seconds_per_day = 24 * 3600;

   class [[fscio::contract("fscio.system")]] system_contract : public native {
      private:
         voters_table            _voters;
         producers_table         _producers;
         global_state_singleton  _global;
         fscio_global_state      _gstate;
         rammarket               _rammarket;

      public:
         static constexpr fscio::name active_permission{"active"_n};
         static constexpr fscio::name token_account{"fscio.token"_n};
         static constexpr fscio::name ram_account{"fscio.ram"_n};
         static constexpr fscio::name ramfee_account{"fscio.ramfee"_n};
         static constexpr fscio::name stake_account{"fscio.stake"_n};
         static constexpr fscio::name bpay_account{"fscio.bpay"_n};
         static constexpr fscio::name vpay_account{"fscio.vpay"_n};
         static constexpr fscio::name names_account{"fscio.names"_n};
         static constexpr fscio::name saving_account{"fscio.saving"_n};
         static constexpr fscio::name resairdrop_account{"fscio.resad"_n};
         static constexpr symbol ramcore_symbol = symbol(symbol_code("RAMCORE"), 4);
         static constexpr symbol ram_symbol     = symbol(symbol_code("RAM"), 0);

         system_contract( name s, name code, datastream<const char*> ds );
         ~system_contract();

         static symbol get_core_symbol( name system_account = "fscio"_n ) {
            rammarket rm(system_account, system_account.value);
            const static auto sym = get_core_symbol( rm );
            return sym;
         }

         // Actions:
         [[fscio::action]]
         void init( unsigned_int version, symbol core );
         [[fscio::action]]
         void onblock( ignore<block_header> header );

         [[fscio::action]]
         void setalimits( name account, int64_t ram_bytes, int64_t net_weight, int64_t cpu_weight );

         [[fscio::action]]
         void setacctram( name account, std::optional<int64_t> ram_bytes );

         [[fscio::action]]
         void setacctnet( name account, std::optional<int64_t> net_weight );

         [[fscio::action]]
         void setacctcpu( name account, std::optional<int64_t> cpu_weight );

         // functions defined in delegate_bandwidth.cpp

         /**
          *  Stakes SYS from the balance of 'from' for the benfit of 'receiver'.
          *  If transfer == true, then 'receiver' can unstake to their account
          *  Else 'from' can unstake at any time.
          */
         [[fscio::action]]
         void delegatebw( name from, name receiver,
                          asset stake_net_quantity, asset stake_cpu_quantity, bool transfer );


         /**
          *  Decreases the total tokens delegated by from to receiver and/or
          *  frees the memory associated with the delegation if there is nothing
          *  left to delegate.
          *
          *  This will cause an immediate reduction in net/cpu bandwidth of the
          *  receiver.
          *
          *  A transaction is scheduled to send the tokens back to 'from' after
          *  the staking period has passed. If existing transaction is scheduled, it
          *  will be canceled and a new transaction issued that has the combined
          *  undelegated amount.
          *
          *  The 'from' account loses voting power as a result of this call and
          *  all producer tallies are updated.
          */
         [[fscio::action]]
         void undelegatebw( name from, name receiver,
                            asset unstake_net_quantity, asset unstake_cpu_quantity );


         /**
          * Increases receiver's ram quota based upon current price and quantity of
          * tokens provided. An inline transfer from receiver to system contract of
          * tokens will be executed.
          */
         [[fscio::action]]
         void buyramkbytes( name payer, name receiver, uint32_t kbytes );

         /**
          *  Reduces quota my kbytes and then performs an inline transfer of tokens
          *  to receiver based upon the average purchase price of the original quota.
          */
         [[fscio::action]]
         void sellram( name account, int64_t kbytes );

         /**
          *  This action is called after the delegation-period to claim all pending
          *  unstaked tokens belonging to owner
          */
         [[fscio::action]]
         void refund( name owner );

         // functions defined in voting.cpp

         [[fscio::action]]
         void regproducer( const name producer, const public_key& producer_key, 
                           const std::string& url, uint16_t location, double commission_rate);

         [[fscio::action]]
         void unregprod( const name producer );

         [[fscio::action]]
         void setram( uint64_t max_ram_size );
         [[fscio::action]]
         void setramrate( uint16_t bytes_per_block );

         [[fscio::action]]
         void voteproducer( const name voter_name, const name producer_name, const asset vote_num );

         [[fscio::action]]
         void setparams( const fscio::blockchain_parameters& params );

         // functions defined in producer_pay.cpp
         [[fscio::action]]
         void claimprod( const name owner);

         [[fscio::action]]
         void claimvoter( const name owner, const name producer );

         [[fscio::action]]
         void setpriv( name account, uint8_t is_priv );

         [[fscio::action]]
         void rmvproducer( name producer );

         [[fscio::action]]
         void updtrevision( uint8_t revision );

         [[fscio::action]]
         void bidname( name bidder, name newname, asset bid );

         [[fscio::action]]
         void bidrefund( name bidder, name newname );

         /**
          * Set resource airdrop limits
          * In the early stage when the main network goes online, 
          * it needs to create accounts for free, so it needs to conduct air drop resources. 
          * This function is used to set the maximum amount of air drop that each account can get
          */
         [[fscio::action]]
         void setresadcfg( uint32_t limit_ram_kbytes, asset limit_net, asset limit_cpu );

      private:
      
         // Functional control variable    
         static constexpr double   max_commission_adjustment_rate   = 0.05;                                     // 5%
         static constexpr uint64_t one_hour_time                    = 3600 *1000000ll;                          // 1hour   
         static constexpr uint64_t one_day_time                     = 24 * one_hour_time;                       // 1days
         static constexpr uint64_t min_commission_adjustment_period = 7 * one_day_time;                         // 7days
         static constexpr uint64_t claim_voter_rewards_preiod       = 1 * one_day_time;                         // 1days
         static constexpr uint64_t claim_prod_rewards_preiod        = 1 * one_day_time;                         // 1days
         static constexpr uint64_t voteage_basis                    = claim_prod_rewards_preiod / 1000000ll;    // claim rewards preiod 's one fifth
         static constexpr uint64_t top_producers_size               = 15;                                       // FSC default 15
         // Implementation details:

         static symbol get_core_symbol( const rammarket& rm ) {
            auto itr = rm.find(ramcore_symbol.raw());
            fscio_assert(itr != rm.end(), "system contract must first be initialized");
            return itr->quote.balance.symbol;
         }

         //defined in fscio.system.cpp
         static fscio_global_state get_default_parameters();
         static time_point current_time_point();
         static block_timestamp current_block_time();

         symbol core_symbol()const;

         void update_ram_supply();

         //defined in delegate_bandwidth.cpp
         void buyram( name payer, name receiver, asset quant );
         void changebw( name from, name receiver,
                        asset stake_net_quantity, asset stake_cpu_quantity, bool transfer );

         //defined in voting.hpp
         void update_elected_producers( block_timestamp timestamp );

         // defined in voting.cpp
         double update_producer_votepay_share( const producers_table::const_iterator& prod_itr,
                                               time_point ct,
                                               double shares_rate, bool reset_to_zero = false );
         double update_total_votepay_share( time_point ct,
                                            double additional_shares_delta = 0.0, double shares_rate_delta = 0.0 );
         int128_t calculate_prod_all_voter_age( const name producer, const time_point distribut_time );

         // defined in producer_pay.cpp
         uint64_t precision_unit_integer( void );
         uint64_t get_min_activated_stake( void );
         void distribute_voters_rewards( const time_point distribut_time, const name producer );
         void require_activated();
   };

} /// fsciosystem
