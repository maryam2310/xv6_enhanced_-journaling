// Log statistics for group commit + integrity
struct log_stats {
  int total_commits;
  int total_ops_grouped;
  int max_group_size;

  //integrity / failure-handling stats
  int checksum_errors;       // blocks that failed checksum on recovery
  int recovered_blocks;      // blocks successfully replayed on recovery
};

extern struct log_stats lstats;
