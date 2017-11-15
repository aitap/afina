#!/usr/bin/perl
use 5.016;
use warnings;
use autodie;
use FindBin '$Bin';
use IPC::Run qw(run start finish);

my @network = qw(blocking nonblocking uv);
my @storage = qw(map_global map_rwlock map_striped);
my @set_proba = qw(0 5 10 15 20 25 30 35 40 45 100);
my $storage_size = 1024;
my $num_connections = 500;
my $num_iterations = 1e5;

my ($cpu_model, $num_cores) = do {
	my $cpuinfo = do { open my $fh, "</proc/cpuinfo"; local $/; <$fh> };
	(
		(($cpuinfo =~ /^model name\t: ([^\n]+)$/m)[0] || die "couldn't determine CPU model"),
		((() = $cpuinfo =~ /^processor\t: \d+$/mg) || die "couldn't determine number of cores")
	)
};

sub run_afina {
	my ($network, $storage) = @_;
	# debugging output will limit the benchmark by terminal speed
	my $h = (
		(start ["$Bin/../build/src/afina", "-n", $network, "-s", $storage], '>&', '/dev/null', '<', '/dev/null')
		or die "failed to run Afina"
	);
	select undef, undef, undef, .25; # wait for Afina to start accepting connections
	return $h;
}

sub run_benchmark {
	my ($sp) = @_;
	my $out;
	run
		[
			"$Bin/../../memcachetest/memcachetest", '-h', '127.0.0.1:8080', '-i', $storage_size,
			'-P', $sp, '-W', $num_connections, '-c', $num_iterations, '-t', $num_cores, '-L', 3
		],
		'>&', \$out
	or die "memcachetest returned error code $?:\n$out";
	
	my %throughput;
	while ($out =~ /(\w+) throughput: ([0-9.]+) s\^-1/g) {
		$throughput{$1} = $2;
	}
	die "failed to parse throughput:\n$out" unless keys %throughput;
	my %latency95p;
	my $kind;
	for (split /\n/, $out) {
		$kind = $1 if /^(\w+) operations:$/;
		if ($kind and /\d+ us/) {
			$latency95p{$kind} = (/(\d+) us/g)[4];
			undef $kind;
		}
	}
	die "failed to parse latency:\n$out" unless keys %latency95p;
	return (\%throughput, \%latency95p);
}

open my $tsv, ">", "$cpu_model $num_cores.txt";
select $tsv;

print "probability";
for my $net (@network) {
	for my $st (@storage) {
		for my $val (qw(Throughput Latency)) {
			print "\t${net}_${st}_Get_${val}\t${net}_${st}_Set_${val}";
		}
	}
}
print "\n";

for my $sp (@set_proba) {
	print $sp;
	for my $net (@network) {
		for my $st (@storage) {
			my $h = run_afina($net, $st);
			
			my ($throughput, $latency) = eval { run_benchmark($sp) };
			my $err = $@;
			$h->signal("KILL");
			$h->finish;
			die "\nERROR: $sp $net $st\n$err" if $err;

			print "\t", $throughput->{Get}//"NaN", "\t", $throughput->{Set}//"NaN", "\t", $latency->{Get}//"NaN", "\t", $latency->{Set}//"NaN";
		}
	}
	print "\n";
}
