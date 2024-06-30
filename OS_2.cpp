#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <list>
#include <atomic>
#include <chrono>
#include <sstream>

using namespace std;

enum ProcessType { FG, BG };

struct Process {
    int pid;
    ProcessType type;
    bool promoted;
    int remaining_time;
};

struct StackNode {
    list<Process> process_list;
    StackNode* next;
};

class DynamicQueue {
private:
    StackNode* top;
    StackNode* bottom;
    int process_count;
    int threshold;
    mutex mtx;
    condition_variable cv;
    atomic<int> next_pid;

public:
    DynamicQueue()
        : top(new StackNode()), bottom(top), process_count(0), next_pid(0) {
        threshold = calculate_threshold();
    }

    ~DynamicQueue() {
        while (bottom) {
            StackNode* temp = bottom;
            bottom = bottom->next;
            delete temp;
        }
    }

    int calculate_threshold() {
        return (process_count / (count_stack_nodes() + 1)) + 1;
    }

    int count_stack_nodes() {
        int count = 0;
        StackNode* node = bottom;
        while (node) {
            count++;
            node = node->next;
        }
        return count;
    }

    void enqueue(ProcessType type) {
        unique_lock<mutex> lock(mtx);
        int pid = next_pid.fetch_add(1);
        Process p = { pid, type, false, rand() % 10 + 1 }; // 임의의 시간을 할당

        if (type == FG) {
            top->process_list.push_back(p);
        }
        else {
            bottom->process_list.push_back(p);
        }

        process_count++;
        threshold = calculate_threshold();
        split_n_merge();
        cv.notify_all();
    }

    Process dequeue() {
        unique_lock<mutex> lock(mtx);
        cv.wait(lock, [this] { return !top->process_list.empty(); });

        Process p = top->process_list.front();
        top->process_list.pop_front();

        if (top->process_list.empty() && top != bottom) {
            StackNode* temp = top;
            top = find_previous_node(top);
            top->next = nullptr;
            delete temp;
        }

        process_count--;
        threshold = calculate_threshold();
        return p;
    }

    void promote() {
        unique_lock<mutex> lock(mtx);
        if (bottom == top) return;

        StackNode* node = bottom;
        while (node->next != top) {
            node = node->next;
        }

        if (!node->process_list.empty()) {
            Process p = node->process_list.front();
            node->process_list.pop_front();
            p.promoted = true;

            if (node->process_list.empty() && node != bottom) {
                StackNode* temp = node;
                node = find_previous_node(node);
                node->next = temp->next;
                delete temp;
            }

            top->process_list.push_back(p);
        }

        cv.notify_all();
    }

    void split_n_merge() {
        StackNode* node = bottom;
        while (node != nullptr) {
            if (node->process_list.size() > threshold) {
                list<Process> temp_list;
                auto it = node->process_list.begin();
                advance(it, node->process_list.size() / 2);
                temp_list.splice(temp_list.end(), node->process_list, it, node->process_list.end());

                StackNode* new_node = new StackNode();
                new_node->process_list = temp_list;
                new_node->next = node->next;
                node->next = new_node;

                if (node == top) {
                    top = new_node;
                }
            }
            node = node->next;
        }
    }

    StackNode* find_previous_node(StackNode* node) {
        StackNode* temp = bottom;
        while (temp->next != node) {
            temp = temp->next;
        }
        return temp;
    }

    void print_state() {
        unique_lock<mutex> lock(mtx);

        stringstream ss;
        ss << "Running: [" << (top->process_list.empty() ? -1 : top->process_list.front().pid) << "]\n";
        ss << "DQ: ";
        StackNode* node = bottom;
        while (node) {
            ss << "[";
            for (const auto& p : node->process_list) {
                ss << p.pid << (p.type == FG ? "F" : "B") << (p.promoted ? "*" : "") << " ";
            }
            ss << "]";
            node = node->next;
        }
        ss << "\n";

        cout << ss.str();
    }
};

void scheduler(DynamicQueue& dq) {
    while (true) {
        this_thread::sleep_for(chrono::seconds(1));
        dq.print_state();
        dq.promote();
    }
}

void shell(DynamicQueue& dq) {
    while (true) {
        this_thread::sleep_for(chrono::seconds(rand() % 5 + 1));
        dq.enqueue(FG);
    }
}

void monitor(DynamicQueue& dq) {
    while (true) {
        this_thread::sleep_for(chrono::seconds(rand() % 5 + 1));
        dq.enqueue(BG);
    }
}

int main() {
    DynamicQueue dq;

    thread scheduler_thread(scheduler, ref(dq));
    thread shell_thread(shell, ref(dq));
    thread monitor_thread(monitor, ref(dq));

    scheduler_thread.join();
    shell_thread.join();
    monitor_thread.join();

    return 0;
}