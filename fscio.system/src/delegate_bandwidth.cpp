/**
 *  @file
 *  @copyright defined in fsc/LICENSE.txt
 */
#include <fscio.system/fscio.system.hpp>

#include <fsciolib/fscio.hpp>
#include <fsciolib/print.hpp>
#include <fsciolib/datastream.hpp>
#include <fsciolib/serialize.hpp>
#include <fsciolib/multi_index.hpp>
#include <fsciolib/privileged.h>
#include <fsciolib/transaction.hpp>

#include <fscio.token/fscio.token.hpp>


#include <cmath>
#include <map>

namespace fsciosystem {
   using fscio::asset;
   using fscio::indexed_by;
   using fscio::const_mem_fun;
   using fscio::print;
   using fscio::permission_level;
   using fscio::time_point_sec;
   using std::map;
   using std::pair;

   static constexpr uint32_t refund_delay_sec = 3*24*3600;
   static constexpr int64_t  ram_gift_bytes = 1400;

   struct [[fscio::table, fscio::contract("fscio.system")]] user_resources {
      name          owner;
      asset         net_weight;
      asset         cpu_weight;
      int64_t       ram_bytes = 0;

      uint64_t primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( user_resources, (owner)(net_weight)(cpu_weight)(ram_bytes) )
   };


   /**
    *  Every user 'from' has a scope/table that uses every receipient 'to' as the primary key.
    */
   struct [[fscio::table, fscio::contract("fscio.system")]] delegated_bandwidth {
      name          from;
      name          to;
      asset         net_weight;
      asset         cpu_weight;

      uint64_t  primary_key()const { return to.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( delegated_bandwidth, (from)(to)(net_weight)(cpu_weight) )

   };

   struct [[fscio::table, fscio::contract("fscio.system")]] refund_request {
      name            owner;
      time_point_sec  request_time;
      fscio::asset    net_amount;
      fscio::asset    cpu_amount;

      uint64_t  primary_key()const { return owner.value; }

      // explicit serialization macro is not necessary, used here only to improve compilation time
      FSCLIB_SERIALIZE( refund_request, (owner)(request_time)(net_amount)(cpu_amount) )
   };

   /**
    *  These tables are designed to be constructed in the scope of the relevant user, this
    *  facilitates simpler API for per-user queries
    */
   typedef fscio::multi_index< "userres"_n, user_resources >      user_resources_table;
   typedef fscio::multi_index< "delband"_n, delegated_bandwidth > del_bandwidth_table;
   typedef fscio::multi_index< "refunds"_n, refund_request >      refunds_table;



   /**
    *  This action will buy an exact amount of ram and bill the payer the current market price.
    */
   void system_contract::buyramkbytes( name payer, name receiver, uint32_t kbytes ) {

      uint64_t bytes = kbytes * 1024ull;

      auto itr = _rammarket.find(ramcore_symbol.raw());
      auto tmp = *itr;
      auto fscout = tmp.convert( asset(bytes, ram_symbol), core_symbol() );

      buyram( payer, receiver, fscout );
   }


   /**
    *  When buying ram the payer irreversiblly transfers quant to system contract and only
    *  the receiver may reclaim the tokens via the sellram action. The receiver pays for the
    *  storage of all database records associated with this action.
    *
    *  RAM is a scarce resource whose supply is defined by global properties max_ram_size. RAM is
    *  priced using the bancor algorithm such that price-per-byte with a constant reserve ratio of 100:1.
    */
   void system_contract::buyram( name payer, name receiver, asset quant )
   {
      require_auth( payer );
      update_ram_supply();

      fscio_assert( quant.symbol == core_symbol(), "must buy ram with core token" );
      fscio_assert( quant.amount > 0, "must purchase a positive amount" );

      /// airdrop memory resources for user 
      if ( payer == resairdrop_account ) {
         fscio_assert( _gstate.res_airdrop_limit_ram_bytes > 0,  "The airdrop memory resource function has been turned off" );
         
         res_airdrop_table  airdrop( _self, _self.value );
         auto airdrop_itr = airdrop.find( receiver.value );
         if( airdrop_itr != airdrop.end() ) {
            fscio_assert( airdrop_itr->res_airdrop_ram == 0,  "memory resources can only be dropped once" ); 
            const auto& itr = _rammarket.get( ramcore_symbol.raw(), "ram market does not exist" );
            auto rambytes(itr);
            auto airdrop_ram_bytes = rambytes.convert( quant, ram_symbol ).amount;
            fscio_assert( airdrop_ram_bytes <= _gstate.res_airdrop_limit_ram_bytes, "The airdrop memory exceeded the maximum limit" );
         
            airdrop.modify( airdrop_itr, same_payer, [&]( auto& resad ) {
               resad.res_airdrop_ram = static_cast<uint32_t>( airdrop_ram_bytes );
            });

         }else {
            fscio_assert( false,  "receiver is not a airdrop account" );   
         }
      }

      auto fee = quant;
      fee.amount = ( fee.amount + 199 ) / 200; /// .5% fee (round up)
      // fee.amount cannot be 0 since that is only possible if quant.amount is 0 which is not allowed by the assert above.
      // If quant.amount == 1, then fee.amount == 1,
      // otherwise if quant.amount > 1, then 0 < fee.amount < quant.amount.
      auto quant_after_fee = quant;
      quant_after_fee.amount -= fee.amount;
      // quant_after_fee.amount should be > 0 if quant.amount > 1.
      // If quant.amount == 1, then quant_after_fee.amount == 0 and the next inline transfer will fail causing the buyram action to fail.

      INLINE_ACTION_SENDER(fscio::token, transfer)(
         token_account, { {payer, active_permission}, {ram_account, active_permission} },
         { payer, ram_account, quant_after_fee, std::string("buy ram") }
      );

      if( fee.amount > 0 ) {
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {payer, active_permission} },
            { payer, ramfee_account, fee, std::string("ram fee") }
         );
      }

      int64_t bytes_out;

      const auto& market = _rammarket.get(ramcore_symbol.raw(), "ram market does not exist");
      _rammarket.modify( market, same_payer, [&]( auto& es ) {
          bytes_out = es.convert( quant_after_fee,  ram_symbol ).amount;
      });

      fscio_assert( bytes_out > 0, "must reserve a positive amount" );

      _gstate.total_ram_bytes_reserved += uint64_t(bytes_out);
      _gstate.total_ram_stake          += quant_after_fee.amount;

      user_resources_table  userres( _self, receiver.value );
      auto res_itr = userres.find( receiver.value );
      if( res_itr ==  userres.end() ) {
         res_itr = userres.emplace( receiver, [&]( auto& res ) {
               res.owner = receiver;
               res.net_weight = asset( 0, core_symbol() );
               res.cpu_weight = asset( 0, core_symbol() );
               res.ram_bytes = bytes_out;
            });
      } else {
         userres.modify( res_itr, receiver, [&]( auto& res ) {
               res.ram_bytes += bytes_out;
            });
      }

      auto voter_itr = _voters.find( res_itr->owner.value );
      if( voter_itr == _voters.end() || !has_field( voter_itr->flags1, voter_info::flags1_fields::ram_managed ) ) {
         int64_t ram_bytes, net, cpu;
         get_resource_limits( res_itr->owner.value, &ram_bytes, &net, &cpu );
         set_resource_limits( res_itr->owner.value, res_itr->ram_bytes + ram_gift_bytes, net, cpu );
      }
   }

  /**
    *  The system contract now buys and sells RAM allocations at prevailing market prices.
    *  This may result in traders buying RAM today in anticipation of potential shortages
    *  tomorrow. Overall this will result in the market balancing the supply and demand
    *  for RAM over time.
    */
   void system_contract::sellram( name account, int64_t kbytes ) {
      require_auth( account );
      update_ram_supply();

      fscio_assert( kbytes > 0, "cannot sell negative byte" );

      uint64_t bytes = kbytes * 1024ull;
      
      // When selling, first subtract airdrop RAM
      uint32_t airdrop_ram_bytes = 0;
      res_airdrop_table  airdrop( _self, _self.value );
      auto airdrop_itr = airdrop.find( account.value );
      if( airdrop_itr != airdrop.end() ) {
         airdrop_ram_bytes = airdrop_itr->res_airdrop_ram;
      }

      user_resources_table  userres( _self, account.value );
      auto res_itr = userres.find( account.value );
      fscio_assert( res_itr != userres.end(), "no resource row" );
      fscio_assert( (res_itr->ram_bytes - airdrop_ram_bytes) >= bytes, "insufficient quota" );

      asset tokens_out;
      auto itr = _rammarket.find(ramcore_symbol.raw());
      _rammarket.modify( itr, same_payer, [&]( auto& es ) {
          /// the cast to int64_t of bytes is safe because we certify bytes is <= quota which is limited by prior purchases
          tokens_out = es.convert( asset(bytes, ram_symbol), core_symbol());
      });

      fscio_assert( tokens_out.amount > 1, "token amount received from selling ram is too low" );

      _gstate.total_ram_bytes_reserved -= static_cast<decltype(_gstate.total_ram_bytes_reserved)>(bytes); // bytes > 0 is asserted above
      _gstate.total_ram_stake          -= tokens_out.amount;

      //// this shouldn't happen, but just in case it does we should prevent it
      fscio_assert( _gstate.total_ram_stake >= 0, "error, attempt to unstake more tokens than previously staked" );

      userres.modify( res_itr, account, [&]( auto& res ) {
          res.ram_bytes -= bytes;
      });

      auto voter_itr = _voters.find( res_itr->owner.value );
      if( voter_itr == _voters.end() || !has_field( voter_itr->flags1, voter_info::flags1_fields::ram_managed ) ) {
         int64_t ram_bytes, net, cpu;
         get_resource_limits( res_itr->owner.value, &ram_bytes, &net, &cpu );
         set_resource_limits( res_itr->owner.value, res_itr->ram_bytes + ram_gift_bytes, net, cpu );
      }

      INLINE_ACTION_SENDER(fscio::token, transfer)(
         token_account, { {ram_account, active_permission}, {account, active_permission} },
         { ram_account, account, asset(tokens_out), std::string("sell ram") }
      );

      auto fee = ( tokens_out.amount + 199 ) / 200; /// .5% fee (round up)
      // since tokens_out.amount was asserted to be at least 2 earlier, fee.amount < tokens_out.amount
      if( fee > 0 ) {
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {account, active_permission} },
            { account, ramfee_account, asset(fee, core_symbol()), std::string("sell ram fee") }
         );
      }
   }

   void system_contract::changebw( name from, name receiver,
                                   const asset stake_net_delta, const asset stake_cpu_delta, bool transfer )
   {
      require_auth( from );
      fscio_assert( stake_net_delta.amount != 0 || stake_cpu_delta.amount != 0, "should stake non-zero amount" );
      fscio_assert( std::abs( (stake_net_delta + stake_cpu_delta).amount )
                     >= std::max( std::abs( stake_net_delta.amount ), std::abs( stake_cpu_delta.amount ) ),
                    "net and cpu deltas cannot be opposite signs" );

      name source_stake_from = from;
      if ( transfer ) {
         from = receiver;
      }

      /// airdrop net or cpu resources for user 
      if ( source_stake_from == resairdrop_account ) {

         asset zero_asset = asset(0, system_contract::get_core_symbol());

         //fscio_assert( transfer == true,  "When dropping network or CPU resources, transfer flag must be true" );
         
         if ( stake_cpu_delta > zero_asset ) {
            fscio_assert( _gstate.res_airdrop_limit_cpu > zero_asset,  "The airdrop cpu resource function has been turned off" );
            fscio_assert( stake_cpu_delta <= _gstate.res_airdrop_limit_cpu, "The airdrop cpu exceeded the maximum limit" );
         }  

         if ( stake_net_delta > zero_asset ) {
            fscio_assert( _gstate.res_airdrop_limit_net > zero_asset,  "The airdrop net resource function has been turned off" );
            fscio_assert( stake_net_delta <= _gstate.res_airdrop_limit_net, "The airdrop net exceeded the maximum limit" );
         }
         
         res_airdrop_table  airdrop( _self, _self.value );
         auto airdrop_itr = airdrop.find( receiver.value );
         if( airdrop_itr != airdrop.end() ) {
            
            if ( stake_cpu_delta > zero_asset ) {
               fscio_assert( airdrop_itr->res_airdrop_cpu == zero_asset,  "cpu resources can only be dropped once" );
               airdrop.modify( airdrop_itr, same_payer, [&]( auto& n ) {
                  n.res_airdrop_cpu = stake_cpu_delta;
               });
            }  

            if ( stake_net_delta > zero_asset ) {
               fscio_assert( airdrop_itr->res_airdrop_net == zero_asset,  "net resources can only be dropped once" );
               airdrop.modify( airdrop_itr, same_payer, [&]( auto& n ) {
                  n.res_airdrop_net = stake_net_delta;
               });
            }

         }else {
            fscio_assert( false,  "receiver is not a airdrop account" );   
         }
      }

      // update stake delegated from "from" to "receiver"
      {
         del_bandwidth_table     del_tbl( _self, from.value );
         auto itr = del_tbl.find( receiver.value );
         if( itr == del_tbl.end() ) {
            itr = del_tbl.emplace( from, [&]( auto& dbo ){
                  dbo.from          = from;
                  dbo.to            = receiver;
                  dbo.net_weight    = stake_net_delta;
                  dbo.cpu_weight    = stake_cpu_delta;
               });
         }
         else {
            del_tbl.modify( itr, same_payer, [&]( auto& dbo ){
                  dbo.net_weight    += stake_net_delta;
                  dbo.cpu_weight    += stake_cpu_delta;
               });
         }
         fscio_assert( 0 <= itr->net_weight.amount, "insufficient staked net bandwidth" );
         fscio_assert( 0 <= itr->cpu_weight.amount, "insufficient staked cpu bandwidth" );
         if ( itr->net_weight.amount == 0 && itr->cpu_weight.amount == 0 ) {
            del_tbl.erase( itr );
         }
      } // itr can be invalid, should go out of scope

      // update totals of "receiver"
      {
         user_resources_table   totals_tbl( _self, receiver.value );
         auto tot_itr = totals_tbl.find( receiver.value );
         if( tot_itr ==  totals_tbl.end() ) {
            tot_itr = totals_tbl.emplace( from, [&]( auto& tot ) {
                  tot.owner = receiver;
                  tot.net_weight    = stake_net_delta;
                  tot.cpu_weight    = stake_cpu_delta;
               });
         } else {
            totals_tbl.modify( tot_itr, from == receiver ? from : same_payer, [&]( auto& tot ) {
                  tot.net_weight    += stake_net_delta;
                  tot.cpu_weight    += stake_cpu_delta;
               });
         }
         fscio_assert( 0 <= tot_itr->net_weight.amount, "insufficient staked total net bandwidth" );
         fscio_assert( 0 <= tot_itr->cpu_weight.amount, "insufficient staked total cpu bandwidth" );

         {
            bool ram_managed = false;
            bool net_managed = false;
            bool cpu_managed = false;

            auto voter_itr = _voters.find( receiver.value );
            if( voter_itr != _voters.end() ) {
               ram_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::ram_managed );
               net_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::net_managed );
               cpu_managed = has_field( voter_itr->flags1, voter_info::flags1_fields::cpu_managed );
            }

            if( !(net_managed && cpu_managed) ) {
               int64_t ram_bytes, net, cpu;
               get_resource_limits( receiver.value, &ram_bytes, &net, &cpu );

               set_resource_limits( receiver.value,
                                    ram_managed ? ram_bytes : std::max( tot_itr->ram_bytes + ram_gift_bytes, ram_bytes ),
                                    net_managed ? net : tot_itr->net_weight.amount,
                                    cpu_managed ? cpu : tot_itr->cpu_weight.amount );
            }
         }

         if ( tot_itr->net_weight.amount == 0 && tot_itr->cpu_weight.amount == 0  && tot_itr->ram_bytes == 0 ) {
            totals_tbl.erase( tot_itr );
         }
      } // tot_itr can be invalid, should go out of scope

      // create refund or update from existing refund
      if ( stake_account != source_stake_from ) { //for fscio both transfer and refund make no sense
         refunds_table refunds_tbl( _self, from.value );
         auto req = refunds_tbl.find( from.value );

         //create/update/delete refund
         auto net_balance = stake_net_delta;
         auto cpu_balance = stake_cpu_delta;
         bool need_deferred_trx = false;


         // net and cpu are same sign by assertions in delegatebw and undelegatebw
         // redundant assertion also at start of changebw to protect against misuse of changebw
         bool is_undelegating = (net_balance.amount + cpu_balance.amount ) < 0;
         bool is_delegating_to_self = (!transfer && from == receiver);

         if( is_delegating_to_self || is_undelegating ) {
            if ( req != refunds_tbl.end() ) { //need to update refund
               refunds_tbl.modify( req, same_payer, [&]( refund_request& r ) {
                  if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) {
                     r.request_time = current_time_point();
                  }
                  r.net_amount -= net_balance;
                  if ( r.net_amount.amount < 0 ) {
                     net_balance = -r.net_amount;
                     r.net_amount.amount = 0;
                  } else {
                     net_balance.amount = 0;
                  }
                  r.cpu_amount -= cpu_balance;
                  if ( r.cpu_amount.amount < 0 ){
                     cpu_balance = -r.cpu_amount;
                     r.cpu_amount.amount = 0;
                  } else {
                     cpu_balance.amount = 0;
                  }
               });

               fscio_assert( 0 <= req->net_amount.amount, "negative net refund amount" ); //should never happen
               fscio_assert( 0 <= req->cpu_amount.amount, "negative cpu refund amount" ); //should never happen

               if ( req->net_amount.amount == 0 && req->cpu_amount.amount == 0 ) {
                  refunds_tbl.erase( req );
                  need_deferred_trx = false;
               } else {
                  need_deferred_trx = true;
               }
            } else if ( net_balance.amount < 0 || cpu_balance.amount < 0 ) { //need to create refund
               refunds_tbl.emplace( from, [&]( refund_request& r ) {
                  r.owner = from;
                  if ( net_balance.amount < 0 ) {
                     r.net_amount = -net_balance;
                     net_balance.amount = 0;
                  } else {
                     r.net_amount = asset( 0, core_symbol() );
                  }
                  if ( cpu_balance.amount < 0 ) {
                     r.cpu_amount = -cpu_balance;
                     cpu_balance.amount = 0;
                  } else {
                     r.cpu_amount = asset( 0, core_symbol() );
                  }
                  r.request_time = current_time_point();
               });
               need_deferred_trx = true;
            } // else stake increase requested with no existing row in refunds_tbl -> nothing to do with refunds_tbl
         } /// end if is_delegating_to_self || is_undelegating

         if ( need_deferred_trx ) {
            fscio::transaction out;
            out.actions.emplace_back( permission_level{from, active_permission},
                                      _self, "refund"_n,
                                      from
            );
            out.delay_sec = refund_delay_sec;
            cancel_deferred( from.value ); // TODO: Remove this line when replacing deferred trxs is fixed
            out.send( from.value, from, true );
         } else {
            cancel_deferred( from.value );
         }

         auto transfer_amount = net_balance + cpu_balance;
         if ( 0 < transfer_amount.amount ) {
            INLINE_ACTION_SENDER(fscio::token, transfer)(
               token_account, { {source_stake_from, active_permission} },
               { source_stake_from, stake_account, asset(transfer_amount), std::string("stake bandwidth") }
            );
         }
      }

      // update voting power
      {
         asset total_update = stake_net_delta + stake_cpu_delta;
         auto from_voter = _voters.find( from.value );
         if( from_voter == _voters.end() ) {
            from_voter = _voters.emplace( from, [&]( auto& v ) {
                  v.owner  = from;
                  v.staked_balance = total_update;
               });
         } else {
            _voters.modify( from_voter, same_payer, [&]( auto& v ) {
                  v.staked_balance += total_update;
               });
         }
         fscio_assert( 0 <= from_voter->staked_balance.amount, "stake for voting cannot be negative");
      }
   }

   void system_contract::delegatebw( name from, name receiver,
                                     asset stake_net_quantity,
                                     asset stake_cpu_quantity, bool transfer )
   {
      asset zero_asset( 0, core_symbol() );
      fscio_assert( stake_cpu_quantity >= zero_asset, "must stake a positive amount" );
      fscio_assert( stake_net_quantity >= zero_asset, "must stake a positive amount" );
      fscio_assert( stake_net_quantity.amount + stake_cpu_quantity.amount > 0, "must stake a positive amount" );
      fscio_assert( !transfer || from != receiver, "cannot use transfer flag if delegating to self" );

      changebw( from, receiver, stake_net_quantity, stake_cpu_quantity, transfer);
   } // delegatebw

   void system_contract::undelegatebw( name from, name receiver,
                                       asset unstake_net_quantity, asset unstake_cpu_quantity )
   {
      asset zero_asset( 0, core_symbol() );
      fscio_assert( unstake_cpu_quantity >= zero_asset, "must unstake a positive amount" );
      fscio_assert( unstake_net_quantity >= zero_asset, "must unstake a positive amount" );
      fscio_assert( unstake_cpu_quantity.amount + unstake_net_quantity.amount > 0, "must unstake a positive amount" );
      fscio_assert( _gstate.total_activated_stake >= get_min_activated_stake(),
                    "cannot undelegate bandwidth until the chain is activated (at least 15% of all tokens participate in voting)" );

      changebw( from, receiver, -unstake_net_quantity, -unstake_cpu_quantity, false);
   } // undelegatebw


   void system_contract::refund( const name owner ) {
      require_auth( owner );

      refunds_table refunds_tbl( _self, owner.value );
      auto req = refunds_tbl.find( owner.value );
      fscio_assert( req != refunds_tbl.end(), "refund request not found" );
      fscio_assert( req->request_time + seconds(refund_delay_sec) <= current_time_point(),
                    "refund is not available yet" );

      INLINE_ACTION_SENDER(fscio::token, transfer)(
         token_account, { {stake_account, active_permission}, {req->owner, active_permission} },
         { stake_account, req->owner, req->net_amount + req->cpu_amount, std::string("unstake") }
      );

      refunds_tbl.erase( req );
   }


} //namespace fsciosystem
