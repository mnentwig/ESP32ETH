close all;
pkg load statistics;
if (0)
a = load('slowFiltered.txt');
b = load('midLowFiltered.txt');
c = load('midHighFiltered.txt');
d = load('fullFiltered.txt');
aRaw = load('slow.txt');
bRaw = load('midLow.txt');
cRaw = load('midHigh.txt');
dRaw = load('full.txt');

end
if 0
figure(); hold on; grid on;
plot(a, 'k');
plot(b, 'b');
plot(c, 'g');
plot(d, 'r');
legend('slow', 'midLow', 'midHigh', 'full');
figure(); hist(a, 300); title('slow');
figure(); hist(b, 300); title('midLow');
figure(); hist(c, 300); title('midHigh');
figure(); hist(d, 300); title('full');

figure(); cdfplot(a); title('slow');
figure(); cdfplot(b); title('midLow');
figure(); cdfplot(c); title('midHigh');
figure(); cdfplot(d); title('full');
end
close all;
% === take difference signal ===
%m = aRaw; m = m(:, 2) - m(:, 1);
% === decimate ===
r_Hz = 500000;
m = b;
dec = 32;
m = m(1:dec:end); r_Hz = r_Hz / dec;

s = 4*8192/dec;
mm = m;
for pos = 1:1000:numel(m)-s-1
	    m = mm;
m = m(pos:pos+s-1);
figure(11); hold on; plot(m);

m = m .* hanning(numel(m));
%m = [m; zeros(size(m))];
m = fft(m);
fb = fd_freqbase(numel(m)) * r_Hz;
m(abs(fb) >= 1500) = 0;
m = ifft(m .* conj(m));
figure(10); hold on;
plot(real(m)); title('autocorrelation');
end
