/**
 *  @file
 *  @copyright defined in fsc/LICENSE.txt
 */
#include <fscio.system/fscio.system.hpp>

#include <fsciolib/fscio.hpp>
#include <fsciolib/crypto.h>
#include <fsciolib/print.hpp>
#include <fsciolib/datastream.hpp>
#include <fsciolib/serialize.hpp>
#include <fsciolib/multi_index.hpp>
#include <fsciolib/privileged.hpp>
#include <fsciolib/singleton.hpp>
#include <fsciolib/transaction.hpp>
#include <fscio.token/fscio.token.hpp>

#include <algorithm>
#include <cmath>

namespace fsciosystem {
   using fscio::indexed_by;
   using fscio::const_mem_fun;
   using fscio::print;
   using fscio::singleton;
   using fscio::transaction;

   /**
    *  This method will create a producer_config and producer_info object for 'producer'
    *
    *  @pre producer is not already registered
    *  @pre producer to register is an account
    *  @pre authority of producer to register
    *
    */
   void system_contract::regproducer( const name producer, const fscio::public_key& producer_key, 
                                      const std::string& url, uint16_t location, double commission_rate ) {
      fscio_assert( url.size() < 512, "url too long" );
      fscio_assert( producer_key != fscio::public_key(), "public key should not be the default value" );
      fscio_assert( 0 <= commission_rate && commission_rate <= 1, "commission rate should >=0 and <= 1" );
      require_auth( producer );

      auto prod = _producers.find( producer.value );
      const auto ct = current_time_point();

      if ( prod != _producers.end() ) {
         /// check the producer's commission_rate adjustment 
         if( prod->commission_rate != commission_rate ){
            fscio_assert( ct - prod->last_commission_rate_adjustment_time > microseconds(min_commission_adjustment_period),
             "The commission ratio has been adjusted, please try again later" );
            
            /// If the commission ratio is reduced, the ratio needs to be met 
            if( prod->commission_rate > commission_rate ){
               auto adjustment_rate = static_cast<double>((commission_rate - prod->commission_rate) / double(prod->commission_rate));
               fscio_assert( adjustment_rate <= max_commission_adjustment_rate,
               "The commission ratio does not meet the adjustment requirements. Please try again after adjustment"); 
            }
            /// Give rewards to voters, but only modify the value of the rewards
            distribute_voters_rewards( ct, producer );

            _producers.modify( prod, producer, [&]( producer_info& info ){
               info.producer_key = producer_key;
               info.is_active    = true;
               info.url          = url;
               info.location     = location;
               info.commission_rate = commission_rate;
               info.last_commission_rate_adjustment_time = ct;
            });
         }else{
            _producers.modify( prod, producer, [&]( producer_info& info ){
               info.producer_key = producer_key;
               info.is_active    = true;
               info.url          = url;
               info.location     = location;

               if ( info.last_claim_time == time_point() ){
                  info.last_claim_time = ct;
               }
            });
         }
      } else {
         _producers.emplace( producer, [&]( producer_info& info ){
            info.owner           = producer;
            info.total_votes     = 0;
            info.producer_key    = producer_key;
            info.is_active       = true;
            info.url             = url;
            info.location        = location;
            info.last_claim_time = ct;
            info.commission_rate = commission_rate;
            info.last_commission_rate_adjustment_time = ct;
            info.last_votepay_share_update = ct;
         });
      }
   }

   void system_contract::unregprod( const name producer ) {
      require_auth( producer );
      
      /// Give rewards to voters, but only modify the value of the rewards 
      auto ct = current_time_point();
      distribute_voters_rewards( ct, producer );
      const auto& prod = _producers.get( producer.value, "producer not found" );
      _producers.modify( prod, same_payer, [&]( producer_info& info ){
         info.deactivate();
      });
   }

   void system_contract::update_elected_producers( block_timestamp block_time ) {
      _gstate.last_producer_schedule_update = block_time;

      auto idx = _producers.get_index<"prototalvote"_n>();

      std::vector< std::pair<fscio::producer_key,uint16_t> > top_producers;
      top_producers.reserve(top_producers_size);

      for ( auto it = idx.cbegin(); it != idx.cend() && top_producers.size() < top_producers_size && 0 < it->total_votes && it->active(); ++it ) {
         top_producers.emplace_back( std::pair<fscio::producer_key,uint16_t>({{it->owner, it->producer_key}, it->location}) );
      }

      if ( top_producers.size() < _gstate.last_producer_schedule_size ) {
         return;
      }

      /// sort by producer name
      std::sort( top_producers.begin(), top_producers.end() );

      std::vector<fscio::producer_key> producers;

      producers.reserve(top_producers.size());
      for( const auto& item : top_producers )
         producers.push_back(item.first);

      auto packed_schedule = pack(producers);

      if( set_proposed_producers( packed_schedule.data(),  packed_schedule.size() ) >= 0 ) {
         _gstate.last_producer_schedule_size = static_cast<decltype(_gstate.last_producer_schedule_size)>( top_producers.size() );
      }
   }

   double stake2vote( int64_t staked ) {
      /// TODO subtract 2080 brings the large numbers closer to this decade
      double weight = int64_t( (now() - (block_timestamp::block_timestamp_epoch / 1000)) / (seconds_per_day * 7) )  / double( 52 );
      return double(staked) * std::pow( 2, weight );
   }

   double system_contract::update_total_votepay_share( time_point ct,
                                                       double additional_shares_delta,
                                                       double shares_rate_delta )
   {
      print(" ------------------update_total_votepay_share begin---------------- \n");
      print("additional_shares_delta = ", additional_shares_delta, "\n");
      print("shares_rate_delta = ", shares_rate_delta, "\n");
      double delta_total_votepay_share = 0.0;
      if( ct > _gstate.last_vpay_state_update ) {
         delta_total_votepay_share = _gstate.total_vpay_share_change_rate
                                       * double( (ct - _gstate.last_vpay_state_update).count() / 1E6 );
      }
      print("shares_rate_delta = ", shares_rate_delta, "\n");
      print("delta_total_votepay_share = ", delta_total_votepay_share, "\n");
      print("_gstate.total_vpay_share_change_rate = ", _gstate.total_vpay_share_change_rate, "\n");
      print("(ct - _gstate.last_vpay_state_update).count() = ", (ct - _gstate.last_vpay_state_update).count(), "\n");

      delta_total_votepay_share += additional_shares_delta;
      if( delta_total_votepay_share < 0 && _gstate.total_producer_votepay_share < -delta_total_votepay_share ) {
         _gstate.total_producer_votepay_share = 0.0;
      } else {
         _gstate.total_producer_votepay_share += delta_total_votepay_share;
      }

      print("delta_total_votepay_share = ", delta_total_votepay_share, "\n");
      print("_gstate.total_producer_votepay_share = ", _gstate.total_producer_votepay_share, "\n");

      if( shares_rate_delta < 0 && _gstate.total_vpay_share_change_rate < -shares_rate_delta ) {
         _gstate.total_vpay_share_change_rate = 0.0;
      } else {
         _gstate.total_vpay_share_change_rate += shares_rate_delta;
      }
      print("_gstate.total_vpay_share_change_rate = ", _gstate.total_vpay_share_change_rate, "\n");
      _gstate.last_vpay_state_update = ct;
      print(" ------------------update_total_votepay_share end---------------- \n");
      return _gstate.total_producer_votepay_share;
   }

   double system_contract::update_producer_votepay_share( const producers_table::const_iterator& prod_itr,
                                                          time_point ct,
                                                          double shares_rate,
                                                          bool reset_to_zero )
   {
      double delta_votepay_share = 0.0;
      if( shares_rate > 0.0 && ct > prod_itr->last_votepay_share_update ) {
         delta_votepay_share = shares_rate * double( (ct - prod_itr->last_votepay_share_update).count() / 1E6 ); // cannot be negative
      }

      double new_votepay_share = prod_itr->votepay_share + delta_votepay_share;
      _producers.modify( prod_itr, same_payer, [&](auto& p) {
         if( reset_to_zero )
            p.votepay_share = 0.0;
         else
            p.votepay_share = new_votepay_share;

         p.last_votepay_share_update = ct;
      } );

      return new_votepay_share;
   }
   
   int128_t system_contract::calculate_prod_all_voter_age( const name producer, const time_point distribut_time ) {
      int128_t total_voter_age = 0;
      auto pitr = _producers.find( producer.value );
      std::vector<name> voters = pitr->voters;
      std::vector<name>::iterator it = voters.begin();
      while( it != voters.end() ) {
         const auto& voter = _voters.get( (*it).value, "voter not found" );
         votes_table votes_tbl( _self, (*it).value );
         const auto& vts = votes_tbl.get( producer.value, "voter have not add votes to the the producer yet" );
         int128_t newest_voteage = static_cast<int128_t>( vts.voteage + (vts.vote_num.amount / precision_unit_integer()) * static_cast<int64_t>( ( distribut_time - vts.voteage_update_time ).count() / voteage_basis ) );
         
         votes_tbl.modify( vts, same_payer, [&]( vote_info & v ) {
            v.voteage = newest_voteage;
            v.voteage_update_time = distribut_time;
         });

         total_voter_age += newest_voteage;
         it++;
      }
      return total_voter_age;
   }

   /**
    *  @pre producers must be sorted from lowest to highest and must be registered and active
    *  @pre if proxy is set then no producers can be voted for
    *  @pre if proxy is set then proxy account must exist and be registered as a proxy
    *  @pre every listed producer or proxy must have been previously registered
    *  @pre voter must authorize this action
    *  @pre voter must have previously staked some FSC for voting
    *  @pre voter->staked must be up to date
    *
    *  @post every producer previously voted for will have vote reduced by previous vote weight
    *  @post every producer newly voted for will have vote increased by new vote amount
    *  @post prior proxy will proxied_vote_weight decremented by previous vote weight
    *  @post new proxy will proxied_vote_weight incremented by new vote weight
    *
    *  If voting for a proxy, the producer votes will not change until the proxy updates their own vote.
    */
   void system_contract::voteproducer( const name voter_name, const name producer_name, const asset vote_num ) {
      require_auth( voter_name );
      //validate input
      print(" voter_name=",name{voter_name}, " producer_name=",name{producer_name}, " vote_num =", vote_num.amount, "\n");
      fscio_assert( vote_num.symbol == system_contract::get_core_symbol(), "symbol precision mismatch" );
      fscio_assert( vote_num.is_valid(), "invalid vote_num" );
      fscio_assert( vote_num.amount >= 0 && vote_num.amount % precision_unit_integer() == 0, "The number of votes must be an integer" );

      auto voter = _voters.find( voter_name.value );
      fscio_assert( voter != _voters.end(), "user must stake before they can vote" ); /// staking creates voter object

      auto prod = _producers.find(producer_name.value);
      fscio_assert( prod != _producers.end() && prod->active(), "producer is not registered" );

      int64_t change_votes = 0; /** Increase or decrease voting num */
      print(" vote_num.amount=", vote_num.amount, "\n");
      print(" voter->last_vote_weight=", voter->last_vote_weight, "\n");
      auto new_vote_weight = stake2vote( vote_num.amount );
      print(" new_vote_weight=", new_vote_weight, "\n");
      double diff_value = 0.0;
      if ( voter->last_vote_weight > 0 ) {
         diff_value -= voter->last_vote_weight;
      }
      print(" diff_value=", diff_value, "\n");
      if( new_vote_weight >= 0 ) {
         diff_value += new_vote_weight;
      }
      print(" diff_value=", diff_value, "\n");
      votes_table votes_tbl( _self, voter_name.value );
      auto vts = votes_tbl.find( producer_name.value );
      auto ct = current_time_point();
      if( vts == votes_tbl.end() ) {
         fscio_assert( vote_num.amount <= voter->staked_balance.amount , "the balance available for the vote is insufficient" );
         change_votes = vote_num.amount;
         votes_tbl.emplace( voter_name,[&]( vote_info & v ) {
            v.producer_name = producer_name;
            v.vote_num = vote_num;
            v.voteage = 0;
            v.voteage_update_time = ct;
            v.vote_weight = new_vote_weight;
         });
      } else {
         change_votes = vote_num.amount - vts-> vote_num.amount;
         fscio_assert( change_votes <= voter-> staked_balance.amount, "need votes change quantity < your staked balance" );

         votes_tbl.modify( vts, same_payer, [&]( vote_info & v ) {
            v.vote_num = vote_num;
            v.vote_weight = new_vote_weight;
         });
      }

      _voters.modify( voter, same_payer, [&]( voter_info & v ) {
         v.staked_balance.amount -= change_votes;
         v.last_vote_weight = new_vote_weight;
      });

      _producers.modify( prod, same_payer, [&]( producer_info & p ) {
         p.total_vote_num.amount += change_votes;
         p.total_voteage         = calculate_prod_all_voter_age( producer_name, ct );
         p.voteage_update_time   = ct;
         p.total_votes           += diff_value;
         if ( p.total_votes < 0 ) { // floating point arithmetics can give small negative numbers
            p.total_votes = 0;
         }
         std::vector<name>::iterator it = find(p.voters.begin(), p.voters.end(), voter_name);
         if( it == p.voters.end() ){
            p.voters.push_back (voter_name);
         }
         _gstate.total_producer_vote_weight += diff_value;
      });

      double delta_change_rate         = 0.0;
      double total_inactive_vpay_share = 0.0;
      double init_total_votes = prod->total_votes;
      const auto last_claim_plus_3days = prod->last_claim_time + microseconds(3 * useconds_per_day);
      bool crossed_threshold       = (last_claim_plus_3days <= ct);
      bool updated_after_threshold = (last_claim_plus_3days <= prod->last_votepay_share_update);
      // Note: updated_after_threshold implies cross_threshold

      double new_votepay_share = update_producer_votepay_share( prod,
                                    ct,
                                    updated_after_threshold ? 0.0 : init_total_votes,
                                    crossed_threshold && !updated_after_threshold // only reset votepay_share once after threshold
                                 );

      if( !crossed_threshold ) {
         delta_change_rate += diff_value;
      } else if( !updated_after_threshold ) { // TODO
         total_inactive_vpay_share += new_votepay_share;
         delta_change_rate -= init_total_votes;// TODO
      }

      print(" total_inactive_vpay_share=", total_inactive_vpay_share, " delta_change_rate=", delta_change_rate, "\n");
      update_total_votepay_share( ct, -total_inactive_vpay_share, delta_change_rate );
      
      _gstate.total_activated_stake += change_votes;
      if(_gstate.total_activated_stake < 0){
         _gstate.total_activated_stake = 0;
      }

      if( _gstate.total_activated_stake >= get_min_activated_stake() && _gstate.thresh_activated_stake_time == time_point() ) {
         _gstate.thresh_activated_stake_time = current_time_point();
      }
   }

} /// namespace fsciosystem
