// Log statistics for group commit
struct log_stats {
  int total_commits;
  int total_ops_grouped;
  int max_group_size;
};

extern struct log_stats lstats;
