#include <fscio.system/fscio.system.hpp>

#include <fscio.token/fscio.token.hpp>
#include <math.h>

namespace fsciosystem {

   const int64_t  min_pervote_daily_pay = 10;               // Minimum num of vote pay
   const int64_t  max_issure_supply     = 150'000'000;      // max supply 1.5
   const double   min_activated_stake_rate = 0.15;          // 15% rate, based on max supply
   const double   continuous_rate       = 0.04879;          // 5% annual rate
   const double   perblock_rate         = 0.2;              // 20% producer block reward
   const double   standby_rate          = 0.3;              // 30% Voting reward
   const double   saving_rate           = 0.5;              // 50% Saving reward
   const uint32_t blocks_per_year       = 52*7*24*2*3600;   // half seconds per year
   const uint32_t seconds_per_year      = 52*7*24*3600;
   const uint32_t blocks_per_day        = 2 * 24 * 3600;
   const uint32_t blocks_per_hour       = 2 * 3600;
   const int64_t  useconds_per_day      = 24 * 3600 * int64_t(1000000);
   const int64_t  useconds_per_year     = seconds_per_year*1000000ll;

   void system_contract::onblock( ignore<block_header> ) {
      using namespace fscio;

      require_auth(_self);

      block_timestamp timestamp;
      name producer;
      _ds >> timestamp >> producer;

      // _gstate.last_block_num is not used anywhere in the system contract code anymore.
      // Although this field is deprecated, we will continue updating it for now until the last_block_num field
      // is eventually completely removed, at which point this line can be removed.
      _gstate.last_block_num = timestamp;

      /** until activated stake crosses this threshold no new rewards are paid */
      if( _gstate.total_activated_stake < get_min_activated_stake() )
         return;

      if( _gstate.last_pervote_bucket_fill == time_point() )  /// start the presses
         _gstate.last_pervote_bucket_fill = current_time_point();


      /**
       * At startup the initial producer may not be one that is registered / elected
       * and therefore there may be no producer object for them.
       */
      auto prod = _producers.find( producer.value );
      if ( prod != _producers.end() ) {
         _gstate.total_unpaid_blocks++;
         _producers.modify( prod, same_payer, [&](auto& p ) {
               p.unpaid_blocks++;
         });
      }

      /// only update block producers once every minute, block_timestamp is in half seconds
      if( timestamp.slot - _gstate.last_producer_schedule_update.slot > 120 ) {
         update_elected_producers( timestamp );

         if( (timestamp.slot - _gstate.last_name_close.slot) > blocks_per_day ) {
            name_bid_table bids(_self, _self.value);
            auto idx = bids.get_index<"highbid"_n>();
            auto highest = idx.lower_bound( std::numeric_limits<uint64_t>::max()/2 );
            if( highest != idx.end() &&
                highest->high_bid > 0 &&
                (current_time_point() - highest->last_bid_time) > microseconds(useconds_per_day) &&
                _gstate.thresh_activated_stake_time > time_point() &&
                (current_time_point() - _gstate.thresh_activated_stake_time) > microseconds(14 * useconds_per_day)
            ) {
               _gstate.last_name_close = timestamp;
               idx.modify( highest, same_payer, [&]( auto& b ){
                  b.high_bid = -b.high_bid;
               });
            }
         }
      }
   }

   using namespace fscio;
   void system_contract::claimprod( const name owner){
      require_auth( owner );
      require_activated();

      const auto& prod = _producers.get( owner.value, "producer not found" );
      auto ct = current_time_point();

      fscio_assert( (ct - prod.last_claim_time).count() >= claim_prod_rewards_preiod, "already claimed rewards within past day" );
      distribute_voters_rewards(ct, owner);
      print("get prpducer rewards, producer is ", name{owner}, "\n");
      if( prod.rewards_producer_block_pay_balance > 0 ) {
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {bpay_account, active_permission}, {owner, active_permission} },
            { bpay_account, owner, asset(prod.rewards_producer_block_pay_balance, core_symbol()), std::string("producer block pay") }
         );
      }
      if( prod.rewards_producer_vote_pay_balance > 0 ) {
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {vpay_account, active_permission}, {owner, active_permission} },
            { vpay_account, owner, asset(prod.rewards_producer_vote_pay_balance, core_symbol()), std::string("producer vote pay") }
         );
      }
      _producers.modify( prod, same_payer, [&](auto& p) {
            p.rewards_producer_block_pay_balance = 0;
            p.rewards_producer_vote_pay_balance = 0;
      });
   }

   void system_contract::claimvoter( const name owner, const name producer ) {
      require_auth( owner );
      require_activated();
      print(" ------------- claimvoter ----------------\n");
      print("owner: ", owner, "\n");
      print("producer: ", producer, "\n");

      auto ct = current_time_point();
      int128_t newest_total_voteage = calculate_prod_all_voter_age( producer, ct );
      
      const auto& voter = _voters.get( owner.value, "voter not found" );
      const auto& prod = _producers.get( producer.value, "producer not found" );
      
      fscio_assert( ( ct - voter.last_claim_time ).count() >= claim_voter_rewards_preiod, "already claimed rewards within past preiod" );

      votes_table votes_tbl( _self, owner.value );
      const auto& vts = votes_tbl.get( producer.value, "voter have not add votes to the the producer yet" );

      int128_t newest_voteage = vts.voteage;
      print("newest_total_voteage = ", newest_total_voteage, "\n");
      fscio_assert( newest_total_voteage > 0, "claim is not available yet" );

      double cut_rate = static_cast<double>( newest_voteage ) / static_cast<double>( newest_total_voteage );

      int64_t vote_reward = static_cast<int64_t>( static_cast<double>( prod.rewards_voters_vote_pay_balance ) * cut_rate );
      int64_t block_reward = static_cast<int64_t>( static_cast<double>( prod.rewards_voters_block_pay_balance ) * cut_rate );

      fscio_assert( 0 <= vote_reward && vote_reward <= prod.rewards_voters_vote_pay_balance, "vote_reward don't count" );
      fscio_assert( 0 <= block_reward && block_reward <= prod.rewards_voters_block_pay_balance, "block_reward don't count" );

      if( vote_reward > 0 ){
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {vpay_account, active_permission}, {owner, active_permission} },
            { vpay_account, owner, asset(vote_reward, core_symbol()), std::string("voter vote pay") }
         );
      }
      
      if( block_reward > 0){
         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {bpay_account, active_permission}, {owner, active_permission} },
            { bpay_account, owner, asset(block_reward, core_symbol()), std::string("voter block pay") }
         );
      }
      
      _voters.modify( voter, same_payer, [&](auto& v) {
         v.last_claim_time = ct;
      });

      votes_tbl.modify( vts, same_payer, [&]( vote_info & v ) {
         v.voteage = 0;
         v.voteage_update_time = ct;
      });

      _producers.modify( prod, same_payer, [&]( producer_info & p ) {
         p.rewards_voters_vote_pay_balance -= vote_reward;
         p.rewards_voters_block_pay_balance -= block_reward;
         p.total_voteage = newest_total_voteage - newest_voteage;
         p.voteage_update_time = ct;
      });
   }

   uint64_t system_contract::precision_unit_integer( void ) {
      return pow( 10, core_symbol().precision() ) ;
   }

   uint64_t system_contract::get_min_activated_stake( void ) {
      return max_issure_supply * ( min_activated_stake_rate * precision_unit_integer() );
   }

   void system_contract::distribute_voters_rewards( const time_point distribut_time, const name producer ) {
      require_activated();
      const auto usecs_since_last_fill = (distribut_time - _gstate.last_pervote_bucket_fill).count();
      if ( usecs_since_last_fill > 0 && _gstate.last_pervote_bucket_fill > time_point() ) {
         const asset token_supply = fscio::token::get_supply(token_account, core_symbol().code());
         print("token_supply is:", token_supply.amount, token_supply.symbol, "\n");
         
         auto new_tokens = static_cast<int64_t>((continuous_rate * static_cast<double>(token_supply.amount) * static_cast<double>(usecs_since_last_fill)) / static_cast<double>(useconds_per_year ));
         print("new_tokens is:", new_tokens, "\n");
         
         auto to_per_block_pay = static_cast<int64_t>(new_tokens * perblock_rate);
         print("to_per_block_pay = ", to_per_block_pay, "\n");

         auto to_per_vote_pay = static_cast<int64_t>(new_tokens * standby_rate);
         print("to_per_vote_pay = ", to_per_vote_pay, "\n");

         auto to_savings = new_tokens - to_per_block_pay - to_per_vote_pay;
         print("to_savings = ", to_savings, "\n");

         INLINE_ACTION_SENDER(fscio::token, issue)(
            token_account, { {_self, active_permission} },
            { _self, asset(new_tokens, core_symbol()), std::string("issue tokens for producer pay and savings") }
         );

         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {_self, active_permission} },
            { _self, saving_account, asset(to_savings, core_symbol()), "unallocated inflation" }
         );

         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {_self, active_permission} },
            { _self, bpay_account, asset(to_per_block_pay, core_symbol()), "fund per-block bucket" }
         );

         INLINE_ACTION_SENDER(fscio::token, transfer)(
            token_account, { {_self, active_permission} },
            { _self, vpay_account, asset(to_per_vote_pay, core_symbol()), "fund per-vote bucket" }
         );

         _gstate.pervote_bucket  += to_per_vote_pay;
         _gstate.perblock_bucket += to_per_block_pay;
         _gstate.last_pervote_bucket_fill = distribut_time;
      }

      auto pitr = _producers.find( producer.value );
      if( pitr != _producers.end() && pitr->active() ) {
         double delta_change_rate         = 0.0;
         double total_inactive_vpay_share = 0.0;
         uint64_t init_total_votes = pitr->total_votes;
         const auto last_claim_plus_3days = pitr->last_claim_time + microseconds(3 * useconds_per_day);
         bool crossed_threshold       = (last_claim_plus_3days <= distribut_time);
         bool updated_after_threshold = (last_claim_plus_3days <= pitr->last_votepay_share_update);
         print(" ------------------update_producer_votepay_share begin---------------- \n");
         print("updated_after_threshold = ", updated_after_threshold, "\n");
         print("init_total_votes = ", init_total_votes, "\n");
         print("crossed_threshold = ", crossed_threshold, "\n");
         print("updated_after_threshold = ", updated_after_threshold, "\n");
         double new_votepay_share = update_producer_votepay_share( pitr,
                                             distribut_time,
                                             updated_after_threshold ? 0.0 : init_total_votes,
                                             true // reset votepay_share to zero after updating
                                          );
         print("new_votepay_share = ", new_votepay_share, "\n");
         double total_votepay_share = update_total_votepay_share( distribut_time );
         print("total_votepay_share = ", total_votepay_share, "\n");
         print(" ------------------update_producer_votepay_share end---------------- \n");

         print(" ------------------producer_per_vote_pay begin---------------- \n");
         print("_gstate.pervote_bucket = ", _gstate.pervote_bucket, "\n");
         int64_t producer_per_vote_pay = 0;
         if( total_votepay_share > 0 && !crossed_threshold ) {
            producer_per_vote_pay = int64_t((new_votepay_share * _gstate.pervote_bucket) / total_votepay_share);
            if( producer_per_vote_pay > _gstate.pervote_bucket )
               producer_per_vote_pay = _gstate.pervote_bucket;
         }
         print("new_votepay_share = ", new_votepay_share, "\n");
         if( producer_per_vote_pay < min_pervote_daily_pay * precision_unit_integer() ) {
            producer_per_vote_pay = 0;
         }
         print("producer_per_vote_pay = ", producer_per_vote_pay, "\n");
         print(" ------------------producer_per_vote_pay end---------------- \n");

         print(" ------------------update_producer_blockpay_share begin---------------- \n");
         uint32_t init_unpaid_blocks = pitr->unpaid_blocks;
         int64_t producer_per_block_pay = 0;
         if( _gstate.total_unpaid_blocks > 0 ) {
            producer_per_block_pay = ( _gstate.perblock_bucket * init_unpaid_blocks ) / _gstate.total_unpaid_blocks;
         }
         print("producer_per_block_pay = ", producer_per_block_pay, "\n");
         print(" ------------------update_producer_blockpay_share end---------------- \n");

         print("producer_per_vote_pay = ", producer_per_vote_pay, "\n");
         print("producer_per_block_pay = ", producer_per_block_pay, "\n");
         print("pitr->commission_rate = ", pitr->commission_rate, "\n");
         int64_t to_voters_vote_reward  = static_cast<int64_t>( producer_per_vote_pay * pitr->commission_rate);
         int64_t to_voters_block_reward  = static_cast<int64_t>( producer_per_block_pay * pitr->commission_rate);
         print("to_voters_vote_reward = ", to_voters_vote_reward, "\n");
         print("to_voters_block_reward = ", to_voters_block_reward, "\n");
         
         print("_gstate.pervote_bucket = ", _gstate.pervote_bucket, "\n");
         print("_gstate.perblock_bucket = ", _gstate.perblock_bucket, "\n");
         print("_gstate.total_unpaid_blocks = ", _gstate.total_unpaid_blocks, "\n");
         _gstate.pervote_bucket      -= producer_per_vote_pay;
         _gstate.perblock_bucket     -= producer_per_block_pay;
         _gstate.total_unpaid_blocks -= init_unpaid_blocks;

         print(" ------------------end---------------- \n");
         print("_gstate.pervote_bucket = ", _gstate.pervote_bucket, "\n");
         print("_gstate.perblock_bucket = ", _gstate.perblock_bucket, "\n");
         print("_gstate.total_unpaid_blocks = ", _gstate.total_unpaid_blocks, "\n");

         update_total_votepay_share( distribut_time, -new_votepay_share, (updated_after_threshold ? init_total_votes : 0.0) );

         print(" ------------------last---------------- \n");
         print("producer_per_vote_pay = ", producer_per_vote_pay, "\n");
         print("producer_per_block_pay = ", producer_per_block_pay, "\n");
         print("init_unpaid_blocks = ", init_unpaid_blocks, "\n");

         _producers.modify(pitr, same_payer, [&](auto &p) {
            p.unpaid_blocks = 0;
            p.last_claim_time = distribut_time;
            p.rewards_voters_block_pay_balance += to_voters_block_reward;
            p.rewards_voters_vote_pay_balance += to_voters_vote_reward;
            p.rewards_producer_block_pay_balance += ( producer_per_block_pay - to_voters_block_reward );
            p.rewards_producer_vote_pay_balance += ( producer_per_vote_pay - to_voters_vote_reward );
            print("p.rewards_voters_block_pay_balance = ", p.rewards_voters_block_pay_balance, "\n");
            print("p.rewards_voters_vote_pay_balance = ", p.rewards_voters_vote_pay_balance, "\n");
            print("p.rewards_producer_block_pay_balance = ", p.rewards_producer_block_pay_balance, "\n");
            print("p.rewards_producer_vote_pay_balance = ", p.rewards_producer_vote_pay_balance, "\n");
         });
      }
   }

   void system_contract::require_activated() {
      fscio_assert( _gstate.total_activated_stake >= get_min_activated_stake(), "cannot claim rewards until the chain is activated" );
   }
} //namespace fsciosystem
